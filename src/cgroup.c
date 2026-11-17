#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "cgroup.h"
#include "paths.h"
#include "container.h"

static int path_is_writable_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        return 0;
    }
    return access(path, W_OK | X_OK) == 0;
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return path_is_writable_dir(path) ? 0 : -1;
    }
    return -1;
}

static int read_self_cgroup_relpath(char *rel, size_t rel_len) {
    FILE *f = fopen("/proc/self/cgroup", "r");
    if (!f) {
        return -1;
    }
    char line[512];
    int found = -1;
    while (fgets(line, sizeof(line), f)) {
        /* cgroup v2 unified: "0::/path" */
        if (strncmp(line, "0::", 3) == 0) {
            char *path = line + 3;
            path[strcspn(path, "\r\n")] = '\0';
            if (path[0] == '\0') {
                strncpy(rel, "/", rel_len - 1);
            } else {
                strncpy(rel, path, rel_len - 1);
            }
            rel[rel_len - 1] = '\0';
            found = 0;
            break;
        }
    }
    fclose(f);
    return found;
}

int cgroup_base_path(char *out, size_t out_len) {
    if (!out || out_len < 16) {
        return -1;
    }

    const char *env = getenv("MINIOS_CGROUP_ROOT");
    if (env && env[0] == '/') {
        if (ensure_dir(env) == 0 && path_is_writable_dir(env)) {
            strncpy(out, env, out_len - 1);
            out[out_len - 1] = '\0';
            return 0;
        }
        fprintf(stderr, "Error: MINIOS_CGROUP_ROOT is not a writable directory: %s\n", env);
        return -1;
    }

    if (ensure_dir(MINIOS_CGROUP_PREFERRED) == 0 &&
        path_is_writable_dir(MINIOS_CGROUP_PREFERRED)) {
        strncpy(out, MINIOS_CGROUP_PREFERRED, out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }

    char rel[PATH_MAX];
    if (read_self_cgroup_relpath(rel, sizeof(rel)) != 0) {
        fprintf(stderr,
                "Error: cannot resolve cgroup v2 base path. "
                "Set MINIOS_CGROUP_ROOT or run with privileges to create %s.\n",
                MINIOS_CGROUP_PREFERRED);
        return -1;
    }

    char candidate[PATH_MAX];
    if (strcmp(rel, "/") == 0) {
        snprintf(candidate, sizeof(candidate), "%s", MINIOS_CGROUP_PREFERRED);
    } else {
        if (snprintf(candidate, sizeof(candidate),
                     "/sys/fs/cgroup%s/lightweight-containers", rel) >= (int)sizeof(candidate)) {
            return -1;
        }
    }

    if (ensure_dir(candidate) != 0 || !path_is_writable_dir(candidate)) {
        fprintf(stderr,
                "Error: cgroup path not writable: %s\n"
                "Hint: enable cgroup v2 delegation or set MINIOS_CGROUP_ROOT.\n",
                candidate);
        return -1;
    }

    /* Automatically enable subtree controllers if root / permitted */
    char sub_path[PATH_MAX];
    if (snprintf(sub_path, sizeof(sub_path), "%s/cgroup.subtree_control", candidate) < (int)sizeof(sub_path)) {
        if (access(sub_path, W_OK) == 0) {
            FILE *sf = fopen(sub_path, "w");
            if (sf) {
                fprintf(sf, "+cpu +memory +pids\n");
                fclose(sf);
            }
        }
    }

    strncpy(out, candidate, out_len - 1);
    out[out_len - 1] = '\0';
    return 0;
}

static int container_cgroup_dir(const char *container_name, char *out, size_t out_len) {
    if (!is_valid_container_name(container_name)) {
        fprintf(stderr, "Error: invalid container name for cgroup.\n");
        return -1;
    }
    char base[PATH_MAX];
    if (cgroup_base_path(base, sizeof(base)) != 0) {
        return -1;
    }
    if (snprintf(out, out_len, "%s/%s", base, container_name) >= (int)out_len) {
        return -1;
    }
    return 0;
}

static int write_to_cgroup_file(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot open cgroup file %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fprintf(f, "%s", value) < 0) {
        fprintf(stderr, "Error: cannot write cgroup file %s: %s\n", path, strerror(errno));
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "Error: failed flushing cgroup file %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

int cgroup_create(const char *container_name) {
    char path[PATH_MAX];
    if (container_cgroup_dir(container_name, path, sizeof(path)) != 0) {
        return -1;
    }

    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        /* Reuse leftover directory from a previous unclean stop. */
        return 0;
    }
    perror("mkdir container cgroup");
    return -1;
}

int cgroup_set_memory_limit(const char *container_name, long limit_mb) {
    char dir[PATH_MAX];
    char path[PATH_MAX];
    char value[64];

    if (container_cgroup_dir(container_name, dir, sizeof(dir)) != 0) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/memory.max", dir) >= (int)sizeof(path)) {
        return -1;
    }
    if (access(path, W_OK) != 0) {
        fprintf(stderr, "Error: memory controller unavailable at %s\n", path);
        return -1;
    }
    snprintf(value, sizeof(value), "%lld", (long long)limit_mb * 1024LL * 1024LL);
    return write_to_cgroup_file(path, value);
}

int cgroup_set_memory_high(const char *container_name, long limit_mb) {
    char dir[PATH_MAX];
    char path[PATH_MAX];
    char value[64];

    if (container_cgroup_dir(container_name, dir, sizeof(dir)) != 0) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/memory.high", dir) >= (int)sizeof(path)) {
        return -1;
    }
    if (access(path, W_OK) != 0) {
        return 0; /* Optional in some setups */
    }
    if (limit_mb <= 0) {
        snprintf(value, sizeof(value), "max");
    } else {
        snprintf(value, sizeof(value), "%lld", (long long)limit_mb * 1024LL * 1024LL);
    }
    return write_to_cgroup_file(path, value);
}

int cgroup_set_swap_limit(const char *container_name, long limit_mb) {
    char dir[PATH_MAX];
    char path[PATH_MAX];
    char value[64];

    if (container_cgroup_dir(container_name, dir, sizeof(dir)) != 0) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/memory.swap.max", dir) >= (int)sizeof(path)) {
        return -1;
    }
    if (access(path, W_OK) != 0) {
        return 0; /* Swap controller might not be enabled or supported */
    }
    if (limit_mb < 0) {
        snprintf(value, sizeof(value), "max");
    } else {
        snprintf(value, sizeof(value), "%lld", (long long)limit_mb * 1024LL * 1024LL);
    }
    return write_to_cgroup_file(path, value);
}

int cgroup_set_oom_group(const char *container_name, int enable) {
    char dir[PATH_MAX];
    char path[PATH_MAX];
    char value[16];

    if (container_cgroup_dir(container_name, dir, sizeof(dir)) != 0) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/memory.oom.group", dir) >= (int)sizeof(path)) {
        return -1;
    }
    if (access(path, W_OK) != 0) {
        return 0;
    }
    snprintf(value, sizeof(value), "%d", enable ? 1 : 0);
    return write_to_cgroup_file(path, value);
}

int cgroup_set_cpu_limit(const char *container_name, int limit_pct) {
    char dir[PATH_MAX];
    char path[PATH_MAX];
    char value[64];

    if (container_cgroup_dir(container_name, dir, sizeof(dir)) != 0) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/cpu.max", dir) >= (int)sizeof(path)) {
        return -1;
    }
    if (access(path, W_OK) != 0) {
        fprintf(stderr, "Error: cpu controller unavailable at %s\n", path);
        return -1;
    }
    /* cpu.max = QUOTA PERIOD; PERIOD=100000us; quota = pct * 1000 */
    long quota = (long)limit_pct * 1000L;
    snprintf(value, sizeof(value), "%ld 100000", quota);
    return write_to_cgroup_file(path, value);
}

int cgroup_set_cpu_weight(const char *container_name, int weight) {
    char dir[PATH_MAX];
    char path[PATH_MAX];
    char value[32];

    if (container_cgroup_dir(container_name, dir, sizeof(dir)) != 0) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/cpu.weight", dir) >= (int)sizeof(path)) {
        return -1;
    }
    if (access(path, W_OK) != 0) {
        return 0;
    }
    if (weight < 1) weight = 1;
    if (weight > 10000) weight = 10000;
    snprintf(value, sizeof(value), "%d", weight);
    return write_to_cgroup_file(path, value);
}

int cgroup_set_cpuset(const char *container_name, const char *cpus) {
    char dir[PATH_MAX];
    char path[PATH_MAX];

    if (!cpus || cpus[0] == '\0') {
        return 0;
    }
    if (container_cgroup_dir(container_name, dir, sizeof(dir)) != 0) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/cpuset.cpus", dir) >= (int)sizeof(path)) {
        return -1;
    }
    if (access(path, W_OK) != 0) {
        return 0;
    }
    return write_to_cgroup_file(path, cpus);
}

int cgroup_set_io_max(const char *container_name, int major, int minor, long rbps, long wbps) {
    char dir[PATH_MAX];
    char path[PATH_MAX];
    char value[128];

    if (container_cgroup_dir(container_name, dir, sizeof(dir)) != 0) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/io.max", dir) >= (int)sizeof(path)) {
        return -1;
    }
    if (access(path, W_OK) != 0) {
        return 0;
    }

    if (rbps > 0 && wbps > 0) {
        snprintf(value, sizeof(value), "%d:%d rbps=%ld wbps=%ld", major, minor, rbps, wbps);
    } else if (rbps > 0) {
        snprintf(value, sizeof(value), "%d:%d rbps=%ld", major, minor, rbps);
    } else if (wbps > 0) {
        snprintf(value, sizeof(value), "%d:%d wbps=%ld", major, minor, wbps);
    } else {
        return 0;
    }
    return write_to_cgroup_file(path, value);
}

int cgroup_attach_task(const char *container_name, pid_t pid) {
    char dir[PATH_MAX];
    char path[PATH_MAX];
    char value[32];

    if (container_cgroup_dir(container_name, dir, sizeof(dir)) != 0) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/cgroup.procs", dir) >= (int)sizeof(path)) {
        return -1;
    }
    snprintf(value, sizeof(value), "%d", (int)pid);
    return write_to_cgroup_file(path, value);
}

int cgroup_remove(const char *container_name) {
    char path[PATH_MAX];
    if (container_cgroup_dir(container_name, path, sizeof(path)) != 0) {
        return -1;
    }
    if (rmdir(path) != 0 && errno != ENOENT) {
        fprintf(stderr, "Warning: could not remove cgroup %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}
