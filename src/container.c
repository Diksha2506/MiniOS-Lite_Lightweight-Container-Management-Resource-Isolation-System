#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>

#include "container.h"
#include "process.h"
#include "cgroup.h"
#include "cleanup.h"
#include "rootfs.h"
#include "paths.h"

bool is_valid_container_name(const char *name) {
    if (!name || name[0] == '\0') {
        return false;
    }
    size_t len = strlen(name);
    if (len > 63) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

static void ensure_metadata_dir(void) {
    mkdir(MINIOS_METADATA_DIR, 0755);
}

int save_container(const container_t *c) {
    ensure_metadata_dir();
    char path[256];
    if (snprintf(path, sizeof(path), "%s/%s.dat", MINIOS_METADATA_DIR, c->name) >= (int)sizeof(path)) {
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    size_t n = fwrite(c, sizeof(container_t), 1, f);
    if (n != 1) {
        fclose(f);
        unlink(path);
        return -1;
    }
    if (fclose(f) != 0) {
        unlink(path);
        return -1;
    }
    return 0;
}

int load_container(const char *name, container_t *c) {
    if (!is_valid_container_name(name) || !c) {
        return -1;
    }
    char path[256];
    if (snprintf(path, sizeof(path), "%s/%s.dat", MINIOS_METADATA_DIR, name) >= (int)sizeof(path)) {
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    memset(c, 0, sizeof(*c));
    size_t n = fread(c, sizeof(container_t), 1, f);
    int ferr = ferror(f);
    fclose(f);
    if (n != 1 || ferr) {
        return -1;
    }
    /* Defensive: ensure strings remain terminated after binary load. */
    c->name[MAX_CONTAINER_NAME - 1] = '\0';
    c->command[MAX_COMMAND_LEN - 1] = '\0';
    c->rootfs[MAX_ROOTFS_LEN - 1] = '\0';
    if (!is_valid_container_name(c->name) || strcmp(c->name, name) != 0) {
        return -1;
    }
    return 0;
}

int delete_container_record(const char *name) {
    if (!is_valid_container_name(name)) {
        return -1;
    }
    char path[256];
    if (snprintf(path, sizeof(path), "%s/%s.dat", MINIOS_METADATA_DIR, name) >= (int)sizeof(path)) {
        return -1;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        return -1;
    }
    return 0;
}

int container_create(const char *name, const char *command, long memory_limit,
                     int cpu_limit, const char *rootfs) {
    if (!is_valid_container_name(name)) {
        fprintf(stderr,
                "Error: Invalid container name '%s'. Name must be 1-63 alphanumeric, '-' or '_'.\n",
                name ? name : "(null)");
        return 1;
    }
    if (!command || command[0] == '\0') {
        fprintf(stderr, "Error: command must not be empty.\n");
        return 1;
    }
    if (strlen(command) >= MAX_COMMAND_LEN) {
        fprintf(stderr, "Error: command too long.\n");
        return 1;
    }
    if (memory_limit < 0 || memory_limit > 1048576L) {
        fprintf(stderr, "Error: Invalid memory limit %ld MB (must be between 0 and 1048576 MB).\n",
                memory_limit);
        return 1;
    }
    if (cpu_limit < 0 || cpu_limit > 100) {
        fprintf(stderr, "Error: Invalid CPU limit %d%% (must be between 0 and 100%%).\n", cpu_limit);
        return 1;
    }

    container_t existing;
    if (load_container(name, &existing) == 0) {
        fprintf(stderr, "Container %s already exists.\n", name);
        return 1;
    }

    container_t c;
    memset(&c, 0, sizeof(c));
    strncpy(c.name, name, MAX_CONTAINER_NAME - 1);
    strncpy(c.command, command, MAX_COMMAND_LEN - 1);

    if (rootfs && rootfs[0] != '\0') {
        if (rootfs_validate(rootfs) != 0) {
            return 1;
        }
        strncpy(c.rootfs, rootfs, MAX_ROOTFS_LEN - 1);
    } else {
        c.rootfs[0] = '\0';
    }

    c.memory_limit_mb = memory_limit;
    c.cpu_limit_pct = cpu_limit;
    c.pid = -1;
    c.state = STATE_STOPPED;

    if (save_container(&c) != 0) {
        perror("save_container");
        return 1;
    }
    printf("Container %s created.\n", name);
    return 0;
}

int container_start(const char *name) {
    if (!is_valid_container_name(name)) {
        fprintf(stderr, "Error: Invalid container name '%s'\n", name ? name : "(null)");
        return 1;
    }

    container_t c;
    if (load_container(name, &c) != 0) {
        fprintf(stderr, "Container %s not found.\n", name);
        return 1;
    }

    /* Reconcile stale RUNNING state before rejecting double-start. */
    if (c.state == STATE_RUNNING) {
        if (c.pid > 0 && kill(c.pid, 0) == 0) {
            fprintf(stderr, "Container %s is already running.\n", name);
            return 1;
        }
        c.state = STATE_STOPPED;
        c.pid = -1;
        save_container(&c);
    }

    if (rootfs_preflight_check(c.name, c.rootfs[0] != '\0' ? c.rootfs : NULL, c.command) != 0) {
        fprintf(stderr, "Preflight check failed for container %s.\n", name);
        return 1;
    }

    if (cgroup_create(c.name) != 0) {
        fprintf(stderr, "Failed to create cgroup.\n");
        return 1;
    }

    if (c.memory_limit_mb > 0 && cgroup_set_memory_limit(c.name, c.memory_limit_mb) != 0) {
        fprintf(stderr, "Failed to set memory limit.\n");
        cgroup_remove(c.name);
        return 1;
    }

    if (c.cpu_limit_pct > 0 && cgroup_set_cpu_limit(c.name, c.cpu_limit_pct) != 0) {
        fprintf(stderr, "Failed to set CPU limit.\n");
        cgroup_remove(c.name);
        return 1;
    }

    pid_t pid = process_start_isolated(&c);
    if (pid < 0) {
        fprintf(stderr, "Failed to start isolated process.\n");
        cgroup_remove(c.name);
        return 1;
    }

    c.pid = pid;
    c.state = STATE_RUNNING;
    if (save_container(&c) != 0) {
        fprintf(stderr, "Warning: container started but metadata save failed.\n");
    }
    printf("Container %s started with PID %d.\n", name, pid);
    return 0;
}

int container_stop(const char *name) {
    if (!is_valid_container_name(name)) {
        fprintf(stderr, "Error: Invalid container name '%s'\n", name ? name : "(null)");
        return 1;
    }

    container_t c;
    if (load_container(name, &c) != 0) {
        fprintf(stderr, "Container %s not found.\n", name);
        return 1;
    }
    if (c.state == STATE_STOPPED) {
        fprintf(stderr, "Container %s is already stopped.\n", name);
        return 1;
    }

    if (c.pid > 0) {
        if (kill(c.pid, SIGKILL) != 0 && errno != ESRCH) {
            fprintf(stderr, "Error: failed to signal PID %d: %s\n", c.pid, strerror(errno));
            return 1;
        }
        waitpid(c.pid, NULL, 0);
        process_release_pid(c.pid);
    }

    c.state = STATE_STOPPED;
    c.pid = -1;
    save_container(&c);

    cleanup_container(name);
    printf("Container %s stopped.\n", name);
    return 0;
}

int container_remove(const char *name) {
    if (!is_valid_container_name(name)) {
        fprintf(stderr, "Error: Invalid container name '%s'\n", name ? name : "(null)");
        return 1;
    }

    container_t c;
    if (load_container(name, &c) != 0) {
        fprintf(stderr, "Container %s not found.\n", name);
        return 1;
    }

    if (c.state == STATE_RUNNING) {
        if (c.pid > 0 && kill(c.pid, 0) == 0) {
            fprintf(stderr, "Cannot remove running container %s. Stop it first.\n", name);
            return 1;
        }
        c.state = STATE_STOPPED;
        c.pid = -1;
        save_container(&c);
    }

    delete_container_record(name);
    cleanup_container(name);
    printf("Container %s removed.\n", name);
    return 0;
}

int container_list(void) {
    ensure_metadata_dir();
    DIR *d = opendir(MINIOS_METADATA_DIR);
    if (!d) {
        perror("opendir");
        return 1;
    }

    struct dirent *dir;
    printf("%-20s %-10s %-10s %-15s %-15s\n",
           "NAME", "PID", "STATE", "MEM_LIMIT(MB)", "CPU_LIMIT(%)");

    while ((dir = readdir(d)) != NULL) {
        size_t len = strlen(dir->d_name);
        if (len < 5 || strcmp(dir->d_name + len - 4, ".dat") != 0) {
            continue;
        }

        char name[MAX_CONTAINER_NAME];
        size_t name_len = len - 4;
        if (name_len >= sizeof(name)) {
            continue;
        }
        memcpy(name, dir->d_name, name_len);
        name[name_len] = '\0';

        if (!is_valid_container_name(name)) {
            continue;
        }

        container_t c;
        if (load_container(name, &c) != 0) {
            continue;
        }

        if (c.state == STATE_RUNNING && (c.pid <= 0 || kill(c.pid, 0) == -1)) {
            pid_t stale_pid = c.pid;
            c.state = STATE_STOPPED;
            c.pid = -1;
            save_container(&c);
            process_release_pid(stale_pid);
        }

        printf("%-20s %-10d %-10s %-15ld %-15d\n",
               c.name,
               c.pid,
               c.state == STATE_RUNNING ? "RUNNING" : "STOPPED",
               c.memory_limit_mb,
               c.cpu_limit_pct);
    }

    closedir(d);
    return 0;
}
