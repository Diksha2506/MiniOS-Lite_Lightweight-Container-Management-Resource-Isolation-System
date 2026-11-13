#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <arpa/inet.h>

#include "network.h"
#include "paths.h"
#include "container.h"

/* Deterministic veth name from container name to fit IFNAMSIZ (15 chars) */
static void make_veth_names(const char *name, char *host_veth, char *peer_veth, size_t max_len) {
    unsigned long hash = 5381;
    for (const char *p = name; *p; p++) {
        hash = ((hash << 5) + hash) + (unsigned char)(*p);
    }
    snprintf(host_veth, max_len, "vh%08lx", hash & 0xFFFFFFFF);
    snprintf(peer_veth, max_len, "vp%08lx", hash & 0xFFFFFFFF);
}

/* Run an executable without shell interpolation */
static int run_argv(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        /* Redirect stdout/stderr to /dev/null */
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return 0;
    }
    return -1;
}

int network_bridge_ensure(const char *bridge_name, const char *bridge_ip, const char *cidr) {
    if (!bridge_name) bridge_name = DEFAULT_BRIDGE_NAME;
    if (!bridge_ip) bridge_ip = DEFAULT_BRIDGE_IP;
    if (!cidr) cidr = DEFAULT_SUBNET_MASK;

    /* Check if bridge already exists: ip link show <bridge_name> */
    char *const show_args[] = {"ip", "link", "show", (char *)bridge_name, NULL};
    if (run_argv(show_args) == 0) {
        /* Bridge exists, ensure it is up */
        char *const up_args[] = {"ip", "link", "set", (char *)bridge_name, "up", NULL};
        run_argv(up_args);
        return 0;
    }

    /* Add bridge: ip link add <bridge_name> type bridge */
    char *const add_args[] = {"ip", "link", "add", (char *)bridge_name, "type", "bridge", NULL};
    if (run_argv(add_args) != 0) {
        fprintf(stderr, "Error: Failed to create bridge %s\n", bridge_name);
        return -1;
    }

    /* Add IP: ip addr add <bridge_ip>/<cidr> dev <bridge_name> */
    char ip_cidr[64];
    snprintf(ip_cidr, sizeof(ip_cidr), "%s/%s", bridge_ip, cidr);
    char *const addr_args[] = {"ip", "addr", "add", ip_cidr, "dev", (char *)bridge_name, NULL};
    if (run_argv(addr_args) != 0) {
        /* Might already have addr, ignore */
    }

    /* Up bridge */
    char *const up_args[] = {"ip", "link", "set", (char *)bridge_name, "up", NULL};
    run_argv(up_args);
    return 0;
}

/* Allocate next available IP in 172.20.0.X (range 2..254) */
static int allocate_ip(const char *container_name, char *out_ip, size_t out_len) {
    char net_dir[PATH_MAX];
    snprintf(net_dir, sizeof(net_dir), "%s/net", MINIOS_METADATA_DIR);
    mkdir(net_dir, 0755);

    /* Check if container already has assigned IP */
    char cont_file[PATH_MAX * 2];
    snprintf(cont_file, sizeof(cont_file), "%s/%s.net", net_dir, container_name);
    FILE *f = fopen(cont_file, "r");
    if (f) {
        char line[64];
        if (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (strncmp(line, "172.20.0.", 9) == 0) {
                strncpy(out_ip, line, out_len - 1);
                out_ip[out_len - 1] = '\0';
                fclose(f);
                return 0;
            }
        }
        fclose(f);
    }

    /* Scan existing containers to mark used IPs */
    for (int oct = 2; oct < 254; oct++) {
        int is_taken = 0;
        char oct_str[32];
        snprintf(oct_str, sizeof(oct_str), "172.20.0.%d", oct);

        DIR *d = opendir(net_dir);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (strstr(ent->d_name, ".net")) {
                    char full[PATH_MAX * 2];
                    snprintf(full, sizeof(full), "%s/%s", net_dir, ent->d_name);
                    FILE *nf = fopen(full, "r");
                    if (nf) {
                        char nip[64];
                        if (fgets(nip, sizeof(nip), nf)) {
                            nip[strcspn(nip, "\r\n")] = '\0';
                            if (strcmp(nip, oct_str) == 0) {
                                is_taken = 1;
                            }
                        }
                        fclose(nf);
                    }
                }
            }
            closedir(d);
        }
        if (!is_taken) {
            snprintf(out_ip, out_len, "%s", oct_str);
            FILE *out_f = fopen(cont_file, "w");
            if (out_f) {
                fprintf(out_f, "%s\n", out_ip);
                fclose(out_f);
            }
            return 0;
        }
    }
    return -1;
}

int network_setup_container(const char *container_name, pid_t pid, network_config_t *out_cfg) {
    if (!container_name || pid <= 0) {
        return -1;
    }

    /* Ensure bridge exists */
    if (network_bridge_ensure(DEFAULT_BRIDGE_NAME, DEFAULT_BRIDGE_IP, DEFAULT_SUBNET_MASK) != 0) {
        return -1;
    }

    char host_veth[32], peer_veth[32];
    make_veth_names(container_name, host_veth, peer_veth, sizeof(host_veth));

    char cont_ip[32];
    if (allocate_ip(container_name, cont_ip, sizeof(cont_ip)) != 0) {
        fprintf(stderr, "Error: No free IP addresses available in subnet.\n");
        return -1;
    }

    /* 1. Create veth pair: ip link add <host_veth> type veth peer name <peer_veth> */
    char *const create_veth[] = {
        "ip", "link", "add", host_veth, "type", "veth", "peer", "name", peer_veth, NULL
    };
    if (run_argv(create_veth) != 0) {
        /* Delete leftover host_veth if exists and retry */
        char *const del_args[] = {"ip", "link", "del", host_veth, NULL};
        run_argv(del_args);
        if (run_argv(create_veth) != 0) {
            fprintf(stderr, "Error: Failed to create veth pair %s <-> %s\n", host_veth, peer_veth);
            return -1;
        }
    }

    /* 2. Attach host_veth to bridge: ip link set <host_veth> master minios-br0 */
    char *const master_args[] = {
        "ip", "link", "set", host_veth, "master", DEFAULT_BRIDGE_NAME, NULL
    };
    if (run_argv(master_args) != 0) {
        fprintf(stderr, "Error: Failed to attach %s to bridge %s\n", host_veth, DEFAULT_BRIDGE_NAME);
    }

    /* 3. Up host_veth: ip link set <host_veth> up */
    char *const up_host[] = {"ip", "link", "set", host_veth, "up", NULL};
    run_argv(up_host);

    /* 4. Move peer_veth into container netns: ip link set <peer_veth> netns <pid> */
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", (int)pid);
    char *const move_netns[] = {
        "ip", "link", "set", peer_veth, "netns", pid_str, NULL
    };
    if (run_argv(move_netns) != 0) {
        fprintf(stderr, "Error: Failed to move %s into netns of PID %d\n", peer_veth, (int)pid);
        char *const del_args[] = {"ip", "link", "del", host_veth, NULL};
        run_argv(del_args);
        return -1;
    }

    /* 5. In container netns, rename peer_veth to eth0:
          ip netns exec (or nsenter) -> we can use nsenter:
          nsenter -t <pid> -n ip link set <peer_veth> name eth0
    */
    char *const rename_args[] = {
        "nsenter", "-t", pid_str, "-n", "ip", "link", "set", peer_veth, "name", "eth0", NULL
    };
    run_argv(rename_args);

    /* 6. Assign IP to eth0:
          nsenter -t <pid> -n ip addr add <cont_ip>/24 dev eth0
    */
    char cont_ip_cidr[64];
    snprintf(cont_ip_cidr, sizeof(cont_ip_cidr), "%s/%s", cont_ip, DEFAULT_SUBNET_MASK);
    char *const assign_ip[] = {
        "nsenter", "-t", pid_str, "-n", "ip", "addr", "add", cont_ip_cidr, "dev", "eth0", NULL
    };
    run_argv(assign_ip);

    /* 7. Bring eth0 up:
          nsenter -t <pid> -n ip link set eth0 up
    */
    char *const up_eth0[] = {
        "nsenter", "-t", pid_str, "-n", "ip", "link", "set", "eth0", "up", NULL
    };
    run_argv(up_eth0);

    /* 8. Bring loopback lo up:
          nsenter -t <pid> -n ip link set lo up
    */
    char *const up_lo[] = {
        "nsenter", "-t", pid_str, "-n", "ip", "link", "set", "lo", "up", NULL
    };
    run_argv(up_lo);

    /* 9. Set default gateway:
          nsenter -t <pid> -n ip route add default via 172.20.0.1
    */
    char *const set_gw[] = {
        "nsenter", "-t", pid_str, "-n", "ip", "route", "add", "default", "via", DEFAULT_BRIDGE_IP, NULL
    };
    run_argv(set_gw);

    if (out_cfg) {
        memset(out_cfg, 0, sizeof(*out_cfg));
        strncpy(out_cfg->container_name, container_name, sizeof(out_cfg->container_name) - 1);
        strncpy(out_cfg->ip_address, cont_ip, sizeof(out_cfg->ip_address) - 1);
        strncpy(out_cfg->gateway, DEFAULT_BRIDGE_IP, sizeof(out_cfg->gateway) - 1);
        strncpy(out_cfg->bridge, DEFAULT_BRIDGE_NAME, sizeof(out_cfg->bridge) - 1);
        strncpy(out_cfg->host_veth, host_veth, sizeof(out_cfg->host_veth) - 1);
        strncpy(out_cfg->cont_veth, "eth0", sizeof(out_cfg->cont_veth) - 1);
        out_cfg->pid = pid;
        out_cfg->is_connected = 1;
    }
    return 0;
}

int network_teardown_container(const char *container_name) {
    if (!container_name) return -1;
    char host_veth[32], peer_veth[32];
    make_veth_names(container_name, host_veth, peer_veth, sizeof(host_veth));

    /* Deleting the host veth automatically cleans up the peer interface in netns */
    char *const del_args[] = {"ip", "link", "del", host_veth, NULL};
    run_argv(del_args);

    /* Remove .net file */
    char cont_file[PATH_MAX];
    snprintf(cont_file, sizeof(cont_file), "%s/net/%s.net", MINIOS_METADATA_DIR, container_name);
    unlink(cont_file);
    return 0;
}

int network_get_config(const char *container_name, network_config_t *cfg) {
    if (!container_name || !cfg) return -1;
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->container_name, container_name, sizeof(cfg->container_name) - 1);
    strncpy(cfg->gateway, DEFAULT_BRIDGE_IP, sizeof(cfg->gateway) - 1);
    strncpy(cfg->bridge, DEFAULT_BRIDGE_NAME, sizeof(cfg->bridge) - 1);
    strncpy(cfg->cont_veth, "eth0", sizeof(cfg->cont_veth) - 1);

    char host_veth[32], peer_veth[32];
    make_veth_names(container_name, host_veth, peer_veth, sizeof(host_veth));
    strncpy(cfg->host_veth, host_veth, sizeof(cfg->host_veth) - 1);

    container_t c;
    if (load_container(container_name, &c) == 0) {
        cfg->pid = c.pid;
        cfg->is_connected = (c.state == STATE_RUNNING);
    }

    char cont_file[PATH_MAX];
    snprintf(cont_file, sizeof(cont_file), "%s/net/%s.net", MINIOS_METADATA_DIR, container_name);
    FILE *f = fopen(cont_file, "r");
    if (f) {
        char line[64];
        if (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = '\0';
            strncpy(cfg->ip_address, line, sizeof(cfg->ip_address) - 1);
        }
        fclose(f);
    } else {
        strncpy(cfg->ip_address, "none", sizeof(cfg->ip_address) - 1);
    }
    return 0;
}

int network_print_human(const network_config_t *cfg) {
    if (!cfg) return -1;
    printf("Network Configuration for Container: %s\n", cfg->container_name);
    printf("--------------------------------------------------\n");
    printf("  Status:          %s\n", cfg->is_connected ? "CONNECTED" : "DISCONNECTED");
    printf("  IP Address:      %s\n", cfg->ip_address[0] ? cfg->ip_address : "none");
    printf("  Gateway:         %s\n", cfg->gateway);
    printf("  Bridge:          %s\n", cfg->bridge);
    printf("  Host Interface:  %s\n", cfg->host_veth);
    printf("  Cont Interface:  %s\n", cfg->cont_veth);
    printf("  Container PID:   %d\n", (int)cfg->pid);
    return 0;
}

int network_print_json(const network_config_t *cfg) {
    if (!cfg) return -1;
    printf("{\"container\":\"%s\",\"connected\":%s,\"ip_address\":\"%s\",\"gateway\":\"%s\",\"bridge\":\"%s\",\"host_interface\":\"%s\",\"container_interface\":\"%s\",\"pid\":%d}\n",
           cfg->container_name,
           cfg->is_connected ? "true" : "false",
           cfg->ip_address[0] ? cfg->ip_address : "",
           cfg->gateway,
           cfg->bridge,
           cfg->host_veth,
           cfg->cont_veth,
           (int)cfg->pid);
    return 0;
}
