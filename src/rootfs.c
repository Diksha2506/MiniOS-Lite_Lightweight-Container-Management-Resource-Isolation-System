#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>

#include "rootfs.h"
#include "paths.h"

static int has_dotdot_segment(const char *path) {
    const char *p = path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0') &&
            (p == path || p[-1] == '/')) {
            return 1;
        }
        p++;
    }
    return 0;
}

int rootfs_validate(const char *rootfs_path) {
    if (!rootfs_path || rootfs_path[0] == '\0') {
        fprintf(stderr, "Error: rootfs path is empty.\n");
        return -1;
    }
    if (rootfs_path[0] != '/') {
        fprintf(stderr, "Error: rootfs must be an absolute path.\n");
        return -1;
    }
    if (has_dotdot_segment(rootfs_path)) {
        fprintf(stderr, "Error: rootfs path must not contain '..' segments.\n");
        return -1;
    }
    if (strlen(rootfs_path) >= MAX_ROOTFS_LEN) {
        fprintf(stderr, "Error: rootfs path is too long.\n");
        return -1;
    }

    struct stat st;
    if (stat(rootfs_path, &st) != 0) {
        perror("rootfs_validate: stat");
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: rootfs is not a directory: %s\n", rootfs_path);
        return -1;
    }
    if (access(rootfs_path, X_OK) != 0) {
        fprintf(stderr, "Error: rootfs is not searchable (need execute permission): %s\n",
                rootfs_path);
        return -1;
    }

    /* Require a usable userspace layout for pivot_root targets. */
    char probe[PATH_MAX];
    snprintf(probe, sizeof(probe), "%s/bin", rootfs_path);
    if (access(probe, F_OK) != 0) {
        snprintf(probe, sizeof(probe), "%s/usr/bin", rootfs_path);
        if (access(probe, F_OK) != 0) {
            fprintf(stderr, "Error: rootfs lacks /bin or /usr/bin: %s\n", rootfs_path);
            return -1;
        }
    }

    return 0;
}

static void first_token(const char *command, char *out, size_t out_len) {
    size_t i = 0;
    while (command[i] && isspace((unsigned char)command[i])) {
        i++;
    }
    if (command[i] == '"' || command[i] == '\'') {
        char quote = command[i++];
        size_t j = 0;
        while (command[i] && command[i] != quote && j + 1 < out_len) {
            out[j++] = command[i++];
        }
        out[j] = '\0';
        return;
    }
    size_t j = 0;
    while (command[i] && !isspace((unsigned char)command[i]) && j + 1 < out_len) {
        out[j++] = command[i++];
    }
    out[j] = '\0';
}

int rootfs_preflight_check(const char *container_name,
                           const char *rootfs_path,
                           const char *command) {
    (void)container_name;

    if (!command || command[0] == '\0') {
        fprintf(stderr, "Error: empty command.\n");
        return -1;
    }

    if (rootfs_path && rootfs_path[0] != '\0') {
        if (rootfs_validate(rootfs_path) != 0) {
            return -1;
        }
    }

    char bin[PATH_MAX];
    first_token(command, bin, sizeof(bin));
    if (bin[0] == '\0') {
        fprintf(stderr, "Error: could not parse command.\n");
        return -1;
    }

    /* Absolute command path: check on host (default) or under custom rootfs. */
    if (bin[0] == '/') {
        if (rootfs_path && rootfs_path[0] != '\0') {
            char full[PATH_MAX];
            if (snprintf(full, sizeof(full), "%s%s", rootfs_path, bin) >= (int)sizeof(full)) {
                fprintf(stderr, "Error: command path too long.\n");
                return -1;
            }
            if (access(full, X_OK) != 0) {
                fprintf(stderr, "Error: command not executable in rootfs: %s\n", full);
                return -1;
            }
        } else if (access(bin, X_OK) != 0) {
            fprintf(stderr, "Error: command not executable: %s\n", bin);
            return -1;
        }
        return 0;
    }

    /* Relative names: require PATH lookup on host for default mode only. */
    if (rootfs_path && rootfs_path[0] != '\0') {
        char full[PATH_MAX * 2];
        int n = snprintf(full, sizeof(full), "%s/bin/%s", rootfs_path, bin);
        if (n > 0 && (size_t)n < sizeof(full) && access(full, X_OK) == 0) {
            return 0;
        }
        n = snprintf(full, sizeof(full), "%s/usr/bin/%s", rootfs_path, bin);
        if (n > 0 && (size_t)n < sizeof(full) && access(full, X_OK) == 0) {
            return 0;
        }
        fprintf(stderr, "Error: command '%s' not found under rootfs bin paths.\n", bin);
        return -1;
    }

    if (access(bin, X_OK) == 0) {
        return 0;
    }

    const char *path_env = getenv("PATH");
    if (!path_env) {
        path_env = "/bin:/usr/bin";
    }
    char path_copy[PATH_MAX];
    strncpy(path_copy, path_env, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *save = NULL;
    for (char *dir = strtok_r(path_copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        char full[PATH_MAX];
        if (snprintf(full, sizeof(full), "%s/%s", dir, bin) >= (int)sizeof(full)) {
            continue;
        }
        if (access(full, X_OK) == 0) {
            return 0;
        }
    }

    fprintf(stderr, "Error: command not found on PATH: %s\n", bin);
    return -1;
}
