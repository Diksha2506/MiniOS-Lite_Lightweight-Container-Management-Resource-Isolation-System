#ifndef CONTAINER_H
#define CONTAINER_H

#include <sys/types.h>
#include <stdbool.h>
#include "rootfs.h"
#include "supervisor.h"

#define MAX_CONTAINER_NAME 64
#define MAX_COMMAND_LEN 256

typedef enum {
    STATE_STOPPED,
    STATE_RUNNING
} container_state_t;

typedef struct {
    char name[MAX_CONTAINER_NAME];
    char command[MAX_COMMAND_LEN];
    char rootfs[MAX_ROOTFS_LEN];
    long memory_limit_mb;
    int cpu_limit_pct;
    pid_t pid;
    container_state_t state;

    /* Phase 2: Extended resource & isolation fields */
    long memory_high_mb;        /* soft reclaim threshold (memory.high); 0 = unset */
    long swap_limit_mb;         /* memory.swap.max; 0 = unset, -1 = unlimited */
    int  nice_value;            /* process nice value -20..19; 0 = default */
    char cpuset[64];            /* cpuset.cpus string e.g. "0-3"; empty = all */
    bool seccomp_enabled;       /* apply default seccomp BPF filter */
    bool drop_caps;             /* drop non-essential capabilities */
    restart_policy_t restart_policy; /* supervisor restart policy */
    long io_rbps_limit;         /* io.max rbps; 0 = unset */
    long io_wbps_limit;         /* io.max wbps; 0 = unset */
} container_t;

/* Name: 1..63 chars, [A-Za-z0-9_-] only. */
bool is_valid_container_name(const char *name);

/* Core APIs */
int container_create(const char *name, const char *command, long memory_limit,
                     int cpu_limit, const char *rootfs);
int container_create_extended(const container_t *cfg);
int container_start(const char *name);
int container_stop(const char *name);
int container_remove(const char *name);
int container_list(void);

/* Data persistence */
int save_container(const container_t *c);
int load_container(const char *name, container_t *c);
int delete_container_record(const char *name);

#endif
