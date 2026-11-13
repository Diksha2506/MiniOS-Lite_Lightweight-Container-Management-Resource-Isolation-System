#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "process.h"
#include "namespace.h"
#include "cgroup.h"
#include "network.h"
#include "paths.h"

#define STACK_SIZE (1024 * 1024)
#define MAX_ARGV 64

struct child_args {
    container_t c;          /* private copy — never mutate caller's metadata */
    int sync_fd;            /* read end: wait for parent cgroup attach */
};

struct child_resources {
    pid_t pid;
    char *stack;
    struct child_args *args;
    struct child_resources *next;
};

static struct child_resources *g_resources;

static void resources_track(pid_t pid, char *stack, struct child_args *args) {
    struct child_resources *node = calloc(1, sizeof(*node));
    if (!node) {
        return;
    }
    node->pid = pid;
    node->stack = stack;
    node->args = args;
    node->next = g_resources;
    g_resources = node;
}

static void resources_release(pid_t pid) {
    struct child_resources **pp = &g_resources;
    while (*pp) {
        if ((*pp)->pid == pid) {
            struct child_resources *dead = *pp;
            *pp = dead->next;
            free(dead->stack);
            free(dead->args);
            free(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}

/* Simple argv splitter supporting single and double quotes. */
static int split_command(const char *command, char *buf, size_t buf_len,
                         char **argv, int max_argv) {
    if (strlen(command) >= buf_len) {
        fprintf(stderr, "Error: command too long to parse.\n");
        return -1;
    }
    memcpy(buf, command, strlen(command) + 1);

    int argc = 0;
    char *p = buf;
    while (*p && argc < max_argv - 1) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }

        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            argv[argc++] = p;
            while (*p && *p != quote) {
                p++;
            }
            if (*p == quote) {
                *p++ = '\0';
            }
        } else {
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) {
                p++;
            }
            if (*p) {
                *p++ = '\0';
            }
        }
    }
    argv[argc] = NULL;
    if (argc == 0) {
        fprintf(stderr, "Error: empty command after parsing.\n");
        return -1;
    }
    return argc;
}

static int child_process(void *arg) {
    struct child_args *args = (struct child_args *)arg;

    /* Block until parent attaches us to the cgroup. */
    char sync_byte;
    if (read(args->sync_fd, &sync_byte, 1) != 1) {
        fprintf(stderr, "Error: cgroup sync failed in child.\n");
        _exit(1);
    }
    close(args->sync_fd);
    args->sync_fd = -1;

    if (sync_byte != 'G') {
        fprintf(stderr, "Error: parent aborted container start.\n");
        _exit(1);
    }

    char log_path[256];
    snprintf(log_path, sizeof(log_path), "%s/%s.log", MINIOS_METADATA_DIR, args->c.name);
    FILE *log_file = fopen(log_path, "w");
    if (log_file) {
        dup2(fileno(log_file), STDOUT_FILENO);
        dup2(fileno(log_file), STDERR_FILENO);
        fclose(log_file);
    }

    const char *rootfs = args->c.rootfs[0] ? args->c.rootfs : NULL;
    if (namespace_setup(args->c.name, rootfs) != 0) {
        fprintf(stderr, "Namespace setup failed\n");
        _exit(1);
    }

    char cmd_buf[MAX_COMMAND_LEN];
    char *argv[MAX_ARGV];
    if (split_command(args->c.command, cmd_buf, sizeof(cmd_buf), argv, MAX_ARGV) < 0) {
        _exit(1);
    }

    execvp(argv[0], argv);
    perror("execvp");
    _exit(1);
}

pid_t process_start_isolated(container_t *c) {
    int sync_pipe[2];
    if (pipe(sync_pipe) != 0) {
        perror("pipe");
        return -1;
    }

    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc stack");
        close(sync_pipe[0]);
        close(sync_pipe[1]);
        return -1;
    }

    struct child_args *args = calloc(1, sizeof(*args));
    if (!args) {
        perror("malloc args");
        free(stack);
        close(sync_pipe[0]);
        close(sync_pipe[1]);
        return -1;
    }

    args->c = *c; /* deep-enough copy of fixed-size struct */
    args->sync_fd = sync_pipe[0];

    int clone_flags = CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWNET | SIGCHLD;
    pid_t pid = clone(child_process, stack + STACK_SIZE, clone_flags, args);
    if (pid < 0) {
        perror("clone");
        free(stack);
        free(args);
        close(sync_pipe[0]);
        close(sync_pipe[1]);
        return -1;
    }

    /* Parent: close read end; attach cgroup; configure networking; then release child. */
    close(sync_pipe[0]);

    if (cgroup_attach_task(c->name, pid) != 0) {
        fprintf(stderr, "Failed to attach task to cgroup.\n");
        /* Abort child */
        if (write(sync_pipe[1], "X", 1) < 0) {
            /* ignore */
        }
        close(sync_pipe[1]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        free(stack);
        free(args);
        return -1;
    }

    /* Configure veth pair & network in container netns before releasing child */
    network_setup_container(c->name, pid, NULL);

    if (write(sync_pipe[1], "G", 1) != 1) {
        perror("sync write");
        close(sync_pipe[1]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        free(stack);
        free(args);
        return -1;
    }
    close(sync_pipe[1]);

    resources_track(pid, stack, args);

    /* Reap immediately if the child already exited (failed setup / short command). */
    int status;
    pid_t finished = waitpid(pid, &status, WNOHANG);
    if (finished == pid) {
        resources_release(pid);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            return -1;
        }
        /* Short-lived success: still report the pid; caller will mark running
         * and list will reconcile via kill(pid,0). */
        return pid;
    }

    return pid;
}

void process_release_pid(pid_t pid) {
    if (pid > 0) {
        resources_release(pid);
    }
}
