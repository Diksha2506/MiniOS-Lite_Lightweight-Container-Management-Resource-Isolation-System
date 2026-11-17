#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <sys/types.h>
#include <stdbool.h>

/* Restart policy for a supervised container process. */
typedef enum {
    RESTART_NEVER = 0,       /* Do not restart on exit (default). */
    RESTART_ON_FAILURE,      /* Restart only if exit code != 0. */
    RESTART_ALWAYS           /* Restart unconditionally. */
} restart_policy_t;

/* Per-container supervisor entry. */
typedef struct {
    char name[64];           /* Container name key. */
    pid_t pid;               /* Watched PID (the clone'd child). */
    restart_policy_t policy; /* What to do when the PID exits. */
    int restart_count;       /* How many times we have restarted so far. */
    int max_restarts;        /* 0 = unlimited. */
    bool active;             /* Slot is in use. */
} supervised_container_t;

/*
 * supervisor_register – begin watching a container PID.
 *   name   : container name (must already exist in metadata store)
 *   pid    : process PID returned by process_start_isolated()
 *   policy : restart policy
 * Returns 0 on success, -1 when the table is full.
 */
int supervisor_register(const char *name, pid_t pid, restart_policy_t policy);

/*
 * supervisor_unregister – remove a container from the watch table.
 * Safe to call with an already-unregistered name.
 */
void supervisor_unregister(const char *name);

/*
 * supervisor_reap_once – non-blocking single pass that reaps any
 * zombie children and optionally restarts them according to policy.
 * Call this periodically from main() or from a SIGCHLD handler.
 * Returns the number of children reaped.
 */
int supervisor_reap_once(void);

/*
 * supervisor_stop_graceful – send SIGTERM, wait up to timeout_sec,
 * then SIGKILL if still alive.  Returns 0 on success, -1 on error.
 */
int supervisor_stop_graceful(const char *name, int timeout_sec);

/*
 * supervisor_policy_name – human-readable policy string.
 */
const char *supervisor_policy_name(restart_policy_t p);

/* Install SIGCHLD handler that calls supervisor_reap_once(). */
void supervisor_install_sigchld_handler(void);

#endif /* SUPERVISOR_H */

