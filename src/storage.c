#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "storage.h"
#include "paths.h"

#define STORAGE_BASE_DIR MINIOS_METADATA_DIR "/storage"

bool storage_overlayfs_supported(void) {
    FILE *f = fopen("/proc/filesystems", "r");
    if (!f) return false;
    char line[128];
    bool supported = false;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "overlay")) {
            supported = true;
            break;
        }
    }
    fclose(f);
    return supported;
}

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

int storage_overlayfs_setup(const char *name, const char *lower_dir, char *merged_out, size_t out_len) {
    if (!name || !lower_dir || !merged_out) return -1;
    if (!storage_overlayfs_supported()) {
        fprintf(stderr, "[storage] OverlayFS is not supported by kernel.\n");
        return -1;
    }

    char container_storage[PATH_MAX];
    snprintf(container_storage, sizeof(container_storage), "%s/%s", STORAGE_BASE_DIR, name);

    char upper[PATH_MAX], work[PATH_MAX], merged[PATH_MAX];
    snprintf(upper, sizeof(upper), "%s/upper", container_storage);
    snprintf(work, sizeof(work), "%s/work", container_storage);
    snprintf(merged, sizeof(merged), "%s/merged", container_storage);

    if (mkdir_p(upper) != 0 || mkdir_p(work) != 0 || mkdir_p(merged) != 0) {
        perror("[storage] Failed to create overlay directories");
        return -1;
    }

    char mount_opts[PATH_MAX * 4];
    snprintf(mount_opts, sizeof(mount_opts), "lowerdir=%s,upperdir=%s,workdir=%s",
             lower_dir, upper, work);

    if (mount("overlay", merged, "overlay", 0, mount_opts) != 0) {
        perror("[storage] mount overlay failed");
        return -1;
    }

    strncpy(merged_out, merged, out_len - 1);
    merged_out[out_len - 1] = '\0';
    return 0;
}

int storage_overlayfs_teardown(const char *name) {
    if (!name) return -1;
    char merged[PATH_MAX];
    snprintf(merged, sizeof(merged), "%s/%s/merged", STORAGE_BASE_DIR, name);

    /* Try unmounting merged overlay */
    if (umount2(merged, MNT_DETACH) != 0 && errno != EINVAL && errno != ENOENT) {
        // umount failed or wasn't mounted
    }

    /* Clean up directories */
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "rm -rf %s/%s", STORAGE_BASE_DIR, name);
    // Fork and rm -rf or simple recursive removal
    pid_t pid = fork();
    if (pid == 0) {
        execlp("rm", "rm", "-rf", merged, NULL);
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
    return 0;
}
