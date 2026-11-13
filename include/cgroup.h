#ifndef CGROUP_H
#define CGROUP_H

#include <sys/types.h>
#include <stddef.h>

/*
 * Resolve the writable cgroup v2 base directory for MiniOS containers.
 * Order: MINIOS_CGROUP_ROOT env, preferred /sys/fs/cgroup/lightweight-containers,
 * then a lightweight-containers child under the current process cgroup.
 * Returns 0 on success and writes an absolute path into out.
 */
int cgroup_base_path(char *out, size_t out_len);

int cgroup_create(const char *container_name);
int cgroup_set_memory_limit(const char *container_name, long limit_mb);
int cgroup_set_cpu_limit(const char *container_name, int limit_pct);
int cgroup_attach_task(const char *container_name, pid_t pid);
int cgroup_remove(const char *container_name);

#endif
