#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cli.h"

/*
 * Privilege policy (Phase 0):
 *  - create / list / stats / remove: unprivileged (operate on metadata / reads)
 *  - start / stop: require root (namespaces, pivot_root, cgroup + killing root children)
 * Rootless user namespaces are NOT supported in Phase 0.
 */
static int command_requires_root(const char *cmd) {
    return strcmp(cmd, "start") == 0 || strcmp(cmd, "stop") == 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  create <name> [--memory <MB>] [--cpu <pct>] "
                        "[--command <cmd>] [--rootfs <path>]\n");
        fprintf(stderr, "  start <name>          (requires root)\n");
        fprintf(stderr, "  stop <name>           (requires root)\n");
        fprintf(stderr, "  list\n");
        fprintf(stderr, "  stats <name>\n");
        fprintf(stderr, "  inspect-ns <name> [--json]\n");
        fprintf(stderr, "  inspect-net <name> [--json]\n");
        fprintf(stderr, "  remove <name>\n");
        exit(EXIT_FAILURE);
    }

    if (command_requires_root(argv[1]) && geteuid() != 0) {
        fprintf(stderr,
                "Error: '%s' requires root privileges (e.g. sudo %s %s ...).\n"
                "Namespace isolation, pivot_root and cgroup attachment need CAP_SYS_ADMIN.\n",
                argv[1], argv[0], argv[1]);
        exit(EXIT_FAILURE);
    }

    return parse_and_execute(argc, argv);
}
