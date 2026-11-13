#ifndef ROOTFS_H
#define ROOTFS_H

#include <stddef.h>

#define MAX_ROOTFS_LEN 4096

/*
 * Validate a custom rootfs directory:
 *  - absolute path
 *  - no ".." traversal segments
 *  - exists, is a directory, searchable (X_OK)
 */
int rootfs_validate(const char *rootfs_path);

/*
 * Pre-start checks: optional custom rootfs validation and command resolvability
 * against the host (default mode) or custom rootfs tree.
 */
int rootfs_preflight_check(const char *container_name,
                           const char *rootfs_path,
                           const char *command);

#endif
