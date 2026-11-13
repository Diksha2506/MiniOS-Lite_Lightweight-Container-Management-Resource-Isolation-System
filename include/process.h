#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>
#include "container.h"

pid_t process_start_isolated(container_t *c);

/* Free clone stack / child args associated with a finished container PID. */
void process_release_pid(pid_t pid);

#endif
