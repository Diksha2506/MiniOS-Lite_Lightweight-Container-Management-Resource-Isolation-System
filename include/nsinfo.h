#ifndef NSINFO_H
#define NSINFO_H

#include <sys/types.h>

#define NS_TYPE_PID  "pid"
#define NS_TYPE_UTS  "uts"
#define NS_TYPE_MNT  "mnt"
#define NS_TYPE_IPC  "ipc"
#define NS_TYPE_NET  "net"
#define NS_TYPE_USER "user"

typedef struct {
    char ns_type[16];       /* pid, uts, mnt, ipc, net, user */
    unsigned long inode;    /* e.g. 4026531836 */
    int available;          /* 1 if inspected, 0 if not accessible/unsupported */
    char path[128];         /* e.g. /proc/<pid>/ns/pid */
} ns_entry_t;

typedef struct {
    pid_t pid;
    char container_name[64];
    ns_entry_t entries[6];  /* pid, uts, mnt, ipc, net, user */
    int count;
} container_nsinfo_t;

/* Inspect all 6 namespaces for a running container or given PID */
int nsinfo_inspect_pid(pid_t pid, container_nsinfo_t *info);
int nsinfo_inspect_container(const char *container_name, container_nsinfo_t *info);

/* CLI display and JSON serializer */
int nsinfo_print_human(const container_nsinfo_t *info);
int nsinfo_print_json(const container_nsinfo_t *info);

#endif

