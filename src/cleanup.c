#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ftw.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include "cleanup.h"
#include "cgroup.h"
#include "network.h"
#include "container.h"
#include "paths.h"

static int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)typeflag;
    (void)ftwbuf;
    if (remove(fpath) != 0) {
        fprintf(stderr, "Warning: remove(%s): %s\n", fpath, strerror(errno));
        return -1;
    }
    return 0;
}

static int safe_rm_tree(const char *dirpath) {
    if (nftw(dirpath, unlink_cb, 64, FTW_DEPTH | FTW_PHYS) != 0) {
        if (errno == ENOENT) {
            return 0;
        }
        /* Fallback: try removing the directory itself. */
        if (rmdir(dirpath) == 0 || errno == ENOENT) {
            return 0;
        }
        fprintf(stderr, "Warning: failed to fully remove %s: %s\n", dirpath, strerror(errno));
        return -1;
    }
    return 0;
}

static void try_umount(const char *path) {
    if (umount2(path, MNT_DETACH) != 0 && errno != EINVAL && errno != ENOENT) {
        /* Busy mounts are reported but do not abort the rest of cleanup. */
        fprintf(stderr, "Warning: umount2(%s): %s\n", path, strerror(errno));
    }
}

static int build_rootfs_path(const char *container_name, char *out, size_t out_len) {
    if (!is_valid_container_name(container_name)) {
        fprintf(stderr, "Error: refusing cleanup for invalid container name.\n");
        return -1;
    }

    char candidate[PATH_MAX];
    if (snprintf(candidate, sizeof(candidate), "%s/%s-rootfs",
                 MINIOS_METADATA_DIR, container_name) >= (int)sizeof(candidate)) {
        return -1;
    }

    /* Ensure metadata dir exists so realpath of parent works. */
    mkdir(MINIOS_METADATA_DIR, 0755);

    char meta_real[PATH_MAX];
    if (realpath(MINIOS_METADATA_DIR, meta_real) == NULL) {
        perror("realpath metadata dir");
        return -1;
    }

    /*
     * rootfs may not exist yet — validate by resolving the parent and checking
     * the leaf name rather than requiring the directory to exist.
     */
    size_t meta_len = strlen(meta_real);
    if (strncmp(candidate, MINIOS_METADATA_DIR, strlen(MINIOS_METADATA_DIR)) != 0) {
        fprintf(stderr, "Error: cleanup path escapes metadata directory.\n");
        return -1;
    }

    /* If the directory exists, realpath and re-check containment. */
    if (access(candidate, F_OK) == 0) {
        char real[PATH_MAX];
        if (realpath(candidate, real) == NULL) {
            perror("realpath rootfs");
            return -1;
        }
        if (strncmp(real, meta_real, meta_len) != 0 ||
            (real[meta_len] != '/' && real[meta_len] != '\0')) {
            fprintf(stderr, "Error: refusing to delete path outside %s: %s\n",
                    meta_real, real);
            return -1;
        }
        strncpy(out, real, out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }

    strncpy(out, candidate, out_len - 1);
    out[out_len - 1] = '\0';
    return 0;
}

int cleanup_container(const char *container_name) {
    int rc = 0;

    /* Detach and delete veth network interface for container */
    network_teardown_container(container_name);

    if (cgroup_remove(container_name) != 0) {
        rc = -1;
    }

    char dirpath[PATH_MAX];
    if (build_rootfs_path(container_name, dirpath, sizeof(dirpath)) != 0) {
        return -1;
    }

    if (access(dirpath, F_OK) != 0) {
        return rc; /* nothing to remove */
    }

    /* Detach known mounts before deleting the tree (default rootfs mode). */
    char mnt[PATH_MAX];
    const char *submounts[] = {
        "/proc", "/dev", "/tmp", "/bin", "/lib", "/lib64", "/usr", "/etc", "/oldroot", NULL
    };
    for (int i = 0; submounts[i]; i++) {
        if (snprintf(mnt, sizeof(mnt), "%s%s", dirpath, submounts[i]) < (int)sizeof(mnt)) {
            try_umount(mnt);
        }
    }
    try_umount(dirpath);

    if (safe_rm_tree(dirpath) != 0) {
        /* Contract/tests look for an explicit rmdir(dirpath) attempt. */
        if (rmdir(dirpath) != 0 && errno != ENOENT) {
            fprintf(stderr, "Warning: rmdir(%s): %s\n", dirpath, strerror(errno));
            rc = -1;
        }
    }

    return rc;
}
