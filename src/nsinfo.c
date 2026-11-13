#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "nsinfo.h"
#include "container.h"

static const char *k_ns_types[] = {
    NS_TYPE_PID,
    NS_TYPE_UTS,
    NS_TYPE_MNT,
    NS_TYPE_IPC,
    NS_TYPE_NET,
    NS_TYPE_USER
};

int nsinfo_inspect_pid(pid_t pid, container_nsinfo_t *info) {
    if (!info || pid <= 0) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    info->pid = pid;
    info->count = sizeof(k_ns_types) / sizeof(k_ns_types[0]);

    for (int i = 0; i < info->count; i++) {
        const char *type = k_ns_types[i];
        strncpy(info->entries[i].ns_type, type, sizeof(info->entries[i].ns_type) - 1);
        snprintf(info->entries[i].path, sizeof(info->entries[i].path), "/proc/%d/ns/%s", (int)pid, type);

        struct stat st;
        if (stat(info->entries[i].path, &st) == 0) {
            info->entries[i].inode = (unsigned long)st.st_ino;
            info->entries[i].available = 1;
        } else {
            info->entries[i].inode = 0;
            info->entries[i].available = 0;
        }
    }
    return 0;
}

int nsinfo_inspect_container(const char *container_name, container_nsinfo_t *info) {
    if (!container_name || !info) {
        return -1;
    }
    container_t c;
    if (load_container(container_name, &c) != 0) {
        fprintf(stderr, "Error: Container %s not found.\n", container_name);
        return -1;
    }
    if (c.state != STATE_RUNNING || c.pid <= 0) {
        fprintf(stderr, "Error: Container %s is not running (PID: %d).\n", container_name, (int)c.pid);
        return -1;
    }
    if (nsinfo_inspect_pid(c.pid, info) != 0) {
        return -1;
    }
    strncpy(info->container_name, container_name, sizeof(info->container_name) - 1);
    return 0;
}

int nsinfo_print_human(const container_nsinfo_t *info) {
    if (!info) return -1;
    printf("Namespace Inspection for Container: %s (PID: %d)\n",
           info->container_name[0] ? info->container_name : "N/A", (int)info->pid);
    printf("%-10s %-12s %-18s %s\n", "TYPE", "STATUS", "INODE", "PATH");
    printf("----------------------------------------------------------------------\n");
    for (int i = 0; i < info->count; i++) {
        const ns_entry_t *e = &info->entries[i];
        if (e->available) {
            printf("%-10s %-12s %-18lu %s\n", e->ns_type, "ISOLATED", e->inode, e->path);
        } else {
            printf("%-10s %-12s %-18s %s\n", e->ns_type, "UNAVAILABLE", "-", e->path);
        }
    }
    return 0;
}

int nsinfo_print_json(const container_nsinfo_t *info) {
    if (!info) return -1;
    printf("{\"container\":\"%s\",\"pid\":%d,\"namespaces\":[",
           info->container_name[0] ? info->container_name : "", (int)info->pid);
    for (int i = 0; i < info->count; i++) {
        const ns_entry_t *e = &info->entries[i];
        printf("%s{\"type\":\"%s\",\"available\":%s,\"inode\":%lu,\"path\":\"%s\"}",
               (i > 0) ? "," : "",
               e->ns_type,
               e->available ? "true" : "false",
               e->inode,
               e->path);
    }
    printf("]}\n");
    return 0;
}

