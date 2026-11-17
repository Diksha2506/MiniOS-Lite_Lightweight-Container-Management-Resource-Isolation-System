#ifndef IPC_H
#define IPC_H

#include <sys/types.h>

/* Setup IPC channel for container (Unix domain socket pair) */
int ipc_setup_channel(const char *name, int *host_fd_out, int *container_fd_out);

/* Cleanup container IPC channel */
void ipc_cleanup_channel(const char *name);

#endif /* IPC_H */

