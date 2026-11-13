#include <stdio.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "namespace.h"
#include "rootfs.h"

// Helper to create dir if not exists
static void ensure_dir(const char *path) {
    mkdir(path, 0755);
}

// Helper for read-only bind mounts
static int bind_mount_ro(const char *src, const char *dest) {
    if (access(src, F_OK) != 0) return 0; // Skip if host doesn't have it
    ensure_dir(dest);
    if (mount(src, dest, NULL, MS_BIND | MS_REC, NULL) != 0) {
        perror("bind mount");
        return -1;
    }
    if (mount(src, dest, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_REC, NULL) != 0) {
        perror("remount ro");
        return -1;
    }
    return 0;
}

int namespace_setup(const char *container_name, const char *rootfs_path) {
    // Set hostname (UTS namespace)
    if (sethostname(container_name, strlen(container_name)) != 0) {
        perror("sethostname");
        return -1;
    }

    // Remount / to make MS_PRIVATE so our mounts do not propagate to host
    if (mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) != 0) {
        perror("mount / private");
        return -1;
    }

    char rootfs[PATH_MAX];
    int is_custom = (rootfs_path != NULL && rootfs_path[0] != '\0');

    if (is_custom) {
        if (rootfs_validate(rootfs_path) != 0) {
            fprintf(stderr, "Error: Invalid rootfs specified: %s\n", rootfs_path);
            return -1;
        }
        if (realpath(rootfs_path, rootfs) == NULL) {
            perror("realpath custom rootfs");
            return -1;
        }
    } else {
        snprintf(rootfs, sizeof(rootfs), "/tmp/containers/%s-rootfs", container_name);
        ensure_dir(rootfs);
    }

    // Bind mount rootfs to itself (required for pivot_root mountpoint requirements)
    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) != 0) {
        perror("mount rootfs to itself");
        return -1;
    }

    char dest[PATH_MAX + 64];

    // Only copy host essentials in default fallback mode
    if (!is_custom) {
        const char *dirs[] = {"/bin", "/lib", "/lib64", "/usr", "/etc"};
        for (int i = 0; i < 5; i++) {
            snprintf(dest, sizeof(dest), "%s%s", rootfs, dirs[i]);
            bind_mount_ro(dirs[i], dest);
        }
    }

    // Mount proc
    snprintf(dest, sizeof(dest), "%s/proc", rootfs);
    ensure_dir(dest);
    if (mount("proc", dest, "proc", 0, NULL) != 0) {
        perror("mount proc");
        return -1;
    }
    
    // Mount tmpfs on /dev and /tmp for container
    snprintf(dest, sizeof(dest), "%s/dev", rootfs);
    ensure_dir(dest);
    if (mount("tmpfs", dest, "tmpfs", 0, NULL) != 0) {
        perror("mount /dev tmpfs");
        return -1;
    }
    
    snprintf(dest, sizeof(dest), "%s/tmp", rootfs);
    ensure_dir(dest);
    if (mount("tmpfs", dest, "tmpfs", 0, NULL) != 0) {
        perror("mount /tmp tmpfs");
        return -1;
    }

    // Create oldroot directory inside target rootfs
    char oldroot[PATH_MAX + 64];
    snprintf(oldroot, sizeof(oldroot), "%s/oldroot", rootfs);
    ensure_dir(oldroot);

    // pivot_root
    if (syscall(SYS_pivot_root, rootfs, oldroot) != 0) {
        perror("pivot_root");
        return -1;
    }

    // chdir to / inside new root
    if (chdir("/") != 0) {
        perror("chdir /");
        return -1;
    }

    // Unmount oldroot (detach)
    if (umount2("/oldroot", MNT_DETACH) != 0) {
        perror("umount oldroot");
        return -1;
    }
    rmdir("/oldroot");

    return 0;
}
