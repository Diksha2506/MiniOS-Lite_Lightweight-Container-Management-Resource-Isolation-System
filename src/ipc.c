#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#include "ipc.h"
#include "paths.h"

int ipc_setup_channel(const char *name, int *host_fd_out, int *container_fd_out) {
    (void)name;
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) {
        perror("[ipc] socketpair");
        return -1;
    }
    *host_fd_out = sv[0];
    *container_fd_out = sv[1];
    return 0;
}

void ipc_cleanup_channel(const char *name) {
    (void)name;
}

