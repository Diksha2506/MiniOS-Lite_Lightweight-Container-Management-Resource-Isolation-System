#ifndef SECURITY_H
#define SECURITY_H

#include <stdbool.h>

/* Drop non-essential Linux capabilities using capset/prctl */
int security_drop_capabilities(void);

/* Apply default seccomp-bpf filter to restrict dangerous syscalls */
int security_apply_seccomp(void);

#endif /* SECURITY_H */

