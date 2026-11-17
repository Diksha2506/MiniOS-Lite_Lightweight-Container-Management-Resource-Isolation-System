#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>

#define MAX_PATH_LEN 512

typedef struct {
    char lower_dir[MAX_PATH_LEN];
    char upper_dir[MAX_PATH_LEN];
    char work_dir[MAX_PATH_LEN];
    char merged_dir[MAX_PATH_LEN];
} overlay_config_t;

/* Check if kernel supports OverlayFS via /proc/filesystems */
bool storage_overlayfs_supported(void);

/* Setup OverlayFS layers for container rootfs.
 * Creates upper, work, and merged directories under /tmp/containers/storage/<name>
 * and mounts overlay at merged_dir.
 * Returns 0 on success, -1 on failure.
 */
int storage_overlayfs_setup(const char *name, const char *lower_dir, char *merged_out, size_t out_len);

/* Unmount and cleanup OverlayFS for container */
int storage_overlayfs_teardown(const char *name);

#endif /* STORAGE_H */

