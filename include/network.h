#ifndef NETWORK_H
#define NETWORK_H

#include <sys/types.h>

#define DEFAULT_BRIDGE_NAME "minios-br0"
#define DEFAULT_BRIDGE_IP   "172.20.0.1"
#define DEFAULT_SUBNET_MASK "24"
#define DEFAULT_SUBNET_CIDR "172.20.0.0/24"

typedef struct {
    char container_name[64];
    char ip_address[32];      /* e.g. 172.20.0.2 */
    char gateway[32];         /* e.g. 172.20.0.1 */
    char bridge[32];          /* e.g. minios-br0 */
    char host_veth[32];       /* e.g. veth_a1b2c */
    char cont_veth[32];       /* e.g. eth0 or cv_a1b2c */
    pid_t pid;
    int is_connected;
} network_config_t;

/* Initialize bridge network on host (idempotent) */
int network_bridge_ensure(const char *bridge_name, const char *bridge_ip, const char *cidr);

/* Allocate an IP and setup veth pair for running container PID */
int network_setup_container(const char *container_name, pid_t pid, network_config_t *out_cfg);

/* Teardown network resources for a container (removes veth pair, releases IP) */
int network_teardown_container(const char *container_name);

/* Inspect network info for a container */
int network_get_config(const char *container_name, network_config_t *cfg);

/* Print network info */
int network_print_human(const network_config_t *cfg);
int network_print_json(const network_config_t *cfg);

#endif

