#ifndef PATHS_H
#define PATHS_H

/* Shared filesystem locations for MiniOS metadata and default rootfs trees. */
#define MINIOS_METADATA_DIR "/tmp/containers"

/*
 * Preferred cgroup v2 project root when running with sufficient privileges.
 * Actual runtime path is resolved by cgroup_base_path() (see cgroup.h) so
 * UID-specific systemd slices are never hard-coded.
 */
#define MINIOS_CGROUP_PREFERRED "/sys/fs/cgroup/lightweight-containers"

#endif
