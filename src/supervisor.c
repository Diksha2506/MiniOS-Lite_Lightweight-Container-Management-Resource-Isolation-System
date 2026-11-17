#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "supervisor.h"
#include "container.h"
#include "process.h"
#include "cgroup.h"

#define SUPERVISOR_MAX_ENTRIES 128

static supervised_container_t g_table[SUPERVISOR_MAX_ENTRIES];
static int g_table_init = 0;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static void table_init(void) {
    if (!g_table_init) {
        memset(g_table, 0, sizeof(g_table));
        g_table_init = 1;
    }
}

static supervised_container_t *find_by_name(const char *name) {
    for (int i = 0; i < SUPERVISOR_MAX_ENTRIES; i++) {
        if (g_table[i].active && strcmp(g_table[i].name, name) == 0) {
            return &g_table[i];
        }
    }
    return NULL;
}

static supervised_container_t *find_by_pid(pid_t pid) {
    for (int i = 0; i < SUPERVISOR_MAX_ENTRIES; i++) {
        if (g_table[i].active && g_table[i].pid == pid) {
            return &g_table[i];
        }
    }
    return NULL;
}

static supervised_container_t *find_free_slot(void) {
    for (int i = 0; i < SUPERVISOR_MAX_ENTRIES; i++) {
        if (!g_table[i].active) {
            return &g_table[i];
        }
    }
    return NULL;
}

/*
 * Attempt to restart a container by name.
 * Returns the new PID (>0) on success, or -1 on failure.
 */
static pid_t restart_container(const char *name) {
    container_t c;
    if (load_container(name, &c) != 0) {
        fprintf(stderr, "[supervisor] Cannot reload metadata for '%s'; not restarting.\n", name);
        return -1;
    }

    /* Recreate cgroup in case it was removed during teardown. */
    if (cgroup_create(c.name) != 0) {
        fprintf(stderr, "[supervisor] cgroup_create failed for '%s'.\n", name);
        return -1;
    }
    if (c.memory_limit_mb > 0) {
        cgroup_set_memory_limit(c.name, c.memory_limit_mb);
    }
    if (c.cpu_limit_pct > 0) {
        cgroup_set_cpu_limit(c.name, c.cpu_limit_pct);
    }

    pid_t new_pid = process_start_isolated(&c);
    if (new_pid < 0) {
        fprintf(stderr, "[supervisor] process_start_isolated failed for '%s'.\n", name);
        cgroup_remove(c.name);
        return -1;
    }

    c.pid   = new_pid;
    c.state = STATE_RUNNING;
    if (save_container(&c) != 0) {
        fprintf(stderr, "[supervisor] Warning: metadata save failed after restart of '%s'.\n", name);
    }

    fprintf(stderr, "[supervisor] Restarted '%s' as PID %d.\n", name, new_pid);
    return new_pid;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int supervisor_register(const char *name, pid_t pid, restart_policy_t policy) {
    table_init();

    if (!name || pid <= 0) {
        return -1;
    }

    /* Deduplication: update if already registered. */
    supervised_container_t *existing = find_by_name(name);
    if (existing) {
        existing->pid    = pid;
        existing->policy = policy;
        return 0;
    }

    supervised_container_t *slot = find_free_slot();
    if (!slot) {
        fprintf(stderr, "[supervisor] Table full; cannot register '%s'.\n", name);
        return -1;
    }

    memset(slot, 0, sizeof(*slot));
    strncpy(slot->name, name, sizeof(slot->name) - 1);
    slot->pid           = pid;
    slot->policy        = policy;
    slot->restart_count = 0;
    slot->max_restarts  = 0;   /* unlimited */
    slot->active        = true;

    return 0;
}

void supervisor_unregister(const char *name) {
    table_init();
    supervised_container_t *e = find_by_name(name);
    if (e) {
        memset(e, 0, sizeof(*e));
    }
}

int supervisor_reap_once(void) {
    table_init();
    int reaped = 0;

    int status;
    pid_t exited;

    /*
     * Drain all terminated children using WNOHANG.
     * waitpid(-1, ...) collects any child, not just registered ones,
     * preventing zombie accumulation.
     */
    while ((exited = waitpid(-1, &status, WNOHANG)) > 0) {
        reaped++;

        supervised_container_t *entry = find_by_pid(exited);
        if (!entry) {
            /* Unknown child — freed resources already, nothing else to do. */
            process_release_pid(exited);
            continue;
        }

        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        int by_signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;

        fprintf(stderr, "[supervisor] '%s' (PID %d) exited: code=%d signal=%d restart=%s\n",
                entry->name, exited, exit_code, by_signal,
                supervisor_policy_name(entry->policy));

        process_release_pid(exited);

        bool should_restart = false;
        switch (entry->policy) {
            case RESTART_ALWAYS:
                should_restart = true;
                break;
            case RESTART_ON_FAILURE:
                should_restart = (exit_code != 0 || by_signal != 0);
                break;
            case RESTART_NEVER:
            default:
                should_restart = false;
                break;
        }

        if (should_restart &&
            (entry->max_restarts == 0 || entry->restart_count < entry->max_restarts)) {

            entry->restart_count++;
            pid_t new_pid = restart_container(entry->name);
            if (new_pid > 0) {
                entry->pid = new_pid;
                /* Keep entry active with updated PID. */
                continue;
            }
            /* Restart failed — fall through to deactivation. */
        }

        /* Mark container stopped in persistent metadata. */
        container_t c;
        if (load_container(entry->name, &c) == 0) {
            c.state = STATE_STOPPED;
            c.pid   = -1;
            save_container(&c);
        }
        memset(entry, 0, sizeof(*entry));
    }

    return reaped;
}

int supervisor_stop_graceful(const char *name, int timeout_sec) {
    table_init();

    supervised_container_t *entry = find_by_name(name);
    pid_t pid = -1;

    if (entry) {
        pid = entry->pid;
        /* Disable restart so SIGTERM doesn't trigger a restart loop. */
        entry->policy = RESTART_NEVER;
    } else {
        /* Try loading from metadata. */
        container_t c;
        if (load_container(name, &c) == 0 && c.pid > 0) {
            pid = c.pid;
        }
    }

    if (pid <= 0 || kill(pid, 0) != 0) {
        /* Already dead or never started. */
        if (entry) {
            supervisor_unregister(name);
        }
        return 0;
    }

    /* Phase 1: SIGTERM. */
    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        perror("[supervisor] SIGTERM");
    }

    /* Wait up to timeout_sec for graceful exit. */
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += (timeout_sec > 0 ? timeout_sec : 5);

    while (kill(pid, 0) == 0) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
            break;
        }
        struct timespec ts = {0, 100000000L}; /* 100 ms */
        nanosleep(&ts, NULL);
    }

    /* Phase 2: SIGKILL if still alive. */
    if (kill(pid, 0) == 0) {
        fprintf(stderr, "[supervisor] '%s' (PID %d) did not exit in %ds; sending SIGKILL.\n",
                name, pid, timeout_sec);
        if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
            perror("[supervisor] SIGKILL");
        }
        waitpid(pid, NULL, 0);
    }

    process_release_pid(pid);
    supervisor_unregister(name);
    return 0;
}

const char *supervisor_policy_name(restart_policy_t p) {
    switch (p) {
        case RESTART_NEVER:      return "never";
        case RESTART_ON_FAILURE: return "on-failure";
        case RESTART_ALWAYS:     return "always";
        default:                 return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* SIGCHLD handler                                                      */
/* ------------------------------------------------------------------ */

static void sigchld_handler(int sig) {
    (void)sig;
    /* supervisor_reap_once() is async-signal-safe enough for our use:
     * waitpid is SAS; fprintf to stderr is not SAS but acceptable for
     * debug-level logging in a development runtime. */
    supervisor_reap_once();
}

void supervisor_install_sigchld_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) != 0) {
        perror("[supervisor] sigaction SIGCHLD");
    }
}

