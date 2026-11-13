#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "container.h"
#include "monitor.h"
#include "nsinfo.h"
#include "network.h"

int parse_and_execute(int argc, char **argv) {
    const char *cmd = argv[1];

    if (strcmp(cmd, "create") == 0) {
        if (argc < 3) {
            fprintf(stderr,
                    "Usage: %s create <name> [--memory <MB>] [--cpu <pct>] "
                    "[--command <cmd>] [--rootfs <path>]\n",
                    argv[0]);
            return 1;
        }
        const char *name = argv[2];
        long mem_limit = 0;
        int cpu_limit = 0;
        const char *command = "/bin/sh";
        const char *rootfs = NULL;

        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--memory") == 0 && i + 1 < argc) {
                mem_limit = strtol(argv[++i], NULL, 10);
            } else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
                cpu_limit = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--command") == 0 && i + 1 < argc) {
                command = argv[++i];
            } else if (strcmp(argv[i], "--rootfs") == 0 && i + 1 < argc) {
                rootfs = argv[++i];
            } else {
                fprintf(stderr, "Unknown create option: %s\n", argv[i]);
                return 1;
            }
        }
        return container_create(name, command, mem_limit, cpu_limit, rootfs);
    } else if (strcmp(cmd, "start") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s start <name>\n", argv[0]);
            return 1;
        }
        return container_start(argv[2]);
    } else if (strcmp(cmd, "stop") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s stop <name>\n", argv[0]);
            return 1;
        }
        return container_stop(argv[2]);
    } else if (strcmp(cmd, "remove") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s remove <name>\n", argv[0]);
            return 1;
        }
        return container_remove(argv[2]);
    } else if (strcmp(cmd, "list") == 0) {
        return container_list();
    } else if (strcmp(cmd, "stats") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s stats <name>\n", argv[0]);
            return 1;
        }
        return monitor_stats(argv[2]);
    } else if (strcmp(cmd, "inspect-ns") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s inspect-ns <name> [--json]\n", argv[0]);
            return 1;
        }
        container_nsinfo_t info;
        if (nsinfo_inspect_container(argv[2], &info) != 0) {
            return 1;
        }
        if (argc >= 4 && strcmp(argv[3], "--json") == 0) {
            return nsinfo_print_json(&info);
        }
        return nsinfo_print_human(&info);
    } else if (strcmp(cmd, "inspect-net") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s inspect-net <name> [--json]\n", argv[0]);
            return 1;
        }
        network_config_t cfg;
        if (network_get_config(argv[2], &cfg) != 0) {
            return 1;
        }
        if (argc >= 4 && strcmp(argv[3], "--json") == 0) {
            return network_print_json(&cfg);
        }
        return network_print_human(&cfg);
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    return 1;
}
