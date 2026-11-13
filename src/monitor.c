#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "monitor.h"
#include "cgroup.h"
#include "container.h"

static int read_cgroup_val(const char *cgroup_dir, const char *file,
                           long *val_out, int *is_max) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", cgroup_dir, file) >= (int)sizeof(path)) {
        return -1;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }

    char buf[64];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    buf[strcspn(buf, "\r\n")] = 0;

    if (strcmp(buf, "max") == 0) {
        if (is_max) {
            *is_max = 1;
        }
        if (val_out) {
            *val_out = -1;
        }
        return 0;
    }

    if (is_max) {
        *is_max = 0;
    }
    char *endptr = NULL;
    long val = strtol(buf, &endptr, 10);
    if (endptr == buf) {
        return -1;
    }

    if (val_out) {
        *val_out = val;
    }
    return 0;
}

int monitor_stats(const char *container_name) {
    if (!is_valid_container_name(container_name)) {
        fprintf(stderr, "Error: invalid container name.\n");
        return 1;
    }

    char base[PATH_MAX];
    if (cgroup_base_path(base, sizeof(base)) != 0) {
        fprintf(stderr, "Error: cannot resolve cgroup base for stats.\n");
        return 1;
    }

    char cgroup_dir[PATH_MAX];
    if (snprintf(cgroup_dir, sizeof(cgroup_dir), "%s/%s", base, container_name) >=
        (int)sizeof(cgroup_dir)) {
        return 1;
    }

    long mem_current = -1;
    int mem_current_is_max = 0;
    int has_mem_current =
        (read_cgroup_val(cgroup_dir, "memory.current", &mem_current, &mem_current_is_max) == 0);

    long mem_max = -1;
    int mem_max_is_max = 0;
    int has_mem_max =
        (read_cgroup_val(cgroup_dir, "memory.max", &mem_max, &mem_max_is_max) == 0);

    char path[PATH_MAX];
    long cpu_usage_usec = -1;
    if (snprintf(path, sizeof(path), "%s/cpu.stat", cgroup_dir) < (int)sizeof(path)) {
        FILE *f = fopen(path, "r");
        if (f) {
            char key[64];
            long val;
            while (fscanf(f, "%63s %ld", key, &val) == 2) {
                if (strcmp(key, "usage_usec") == 0) {
                    cpu_usage_usec = val;
                    break;
                }
            }
            fclose(f);
        }
    }

    printf("Stats for container: %s\n", container_name);
    if (has_mem_current && mem_current >= 0) {
        printf("  Memory Usage: %ld MB\n", mem_current / (1024 * 1024));
    } else {
        printf("  Memory Usage: N/A\n");
    }

    if (has_mem_max) {
        if (mem_max_is_max) {
            printf("  Memory Limit: max\n");
        } else if (mem_max >= 0) {
            printf("  Memory Limit: %ld MB\n", mem_max / (1024 * 1024));
        } else {
            printf("  Memory Limit: N/A\n");
        }
    } else {
        printf("  Memory Limit: N/A\n");
    }

    if (cpu_usage_usec >= 0) {
        printf("  CPU Usage Time: %ld ms\n", cpu_usage_usec / 1000);
    } else {
        printf("  CPU Usage Time: N/A\n");
    }

    return 0;
}
