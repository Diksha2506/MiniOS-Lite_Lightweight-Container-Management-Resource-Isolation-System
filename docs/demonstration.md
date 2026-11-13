# Demonstration Workflow

This document provides a step-by-step workflow to demonstrate the Lightweight OS-Level Containerization system for an Operating Systems project presentation.

## Setup

1. **Environment:** Use a native Ubuntu Linux machine or a Virtual Machine (e.g., VirtualBox, WSL2) running Ubuntu. Ensure cgroups v2 is enabled.
2. **Compile:** Run `make` to build the `container` executable.
3. **Privileges:** All commands must be run as `root` (using `sudo`).

## Step 1: Create and Start a Container

Show that we can create an isolated environment with resource constraints.

### Standard Host-Mirrored Container (Default)
When no custom root filesystem is specified, MiniOS-Lite sets up an overlay environment at `/tmp/containers/{name}-rootfs` with read-only bind mounts of host essential binaries (`/bin`, `/lib`, `/usr`, `/etc`).
Note: `create`, `list`, `stats`, `stop`, and `remove` work as non-root; `start` requires root privileges for `clone()` isolation flags and `pivot_root`.

```bash
# Create a container named 'demo1' with 100MB memory limit and 50% CPU limit
./container create demo1 --memory 100 --cpu 50 --command "/bin/sh"

# List containers (shows demo1 as STOPPED)
./container list

# Start the container (requires root / sudo for namespace & pivot_root isolation)
sudo ./container start demo1

# List again (shows demo1 as RUNNING)
./container list
```

### Custom Root Filesystem (Alpine Linux / Busybox rootfs)
To isolate filesystem state completely from the host OS, provide an extracted Linux root filesystem via `--rootfs`:

```bash
# 1. Download and extract an Alpine Linux minirootfs into a dedicated directory
mkdir -p /tmp/alpine-rootfs
curl -sSL https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.1-x86_64.tar.gz | tar -xz -C /tmp/alpine-rootfs

# 2. Create the container pointing to the custom rootfs
./container create alpine-box --memory 128 --cpu 50 --rootfs /tmp/alpine-rootfs --command "/bin/sh -c 'uname -a && cat /etc/os-release'"

# 3. Start the container
sudo ./container start alpine-box
```

*What happens under the hood:*
1. **Validation**: Preflight checks verify that `/tmp/alpine-rootfs` exists, is a directory without `..` traversal, and contains the command executable (`/bin/sh`).
2. **Mount Namespace & pivot_root**: Mounts are set to `MS_PRIVATE`, the custom rootfs is bind-mounted onto itself, private `/proc`, `/dev`, and `/tmp` mountpoints are established, and `pivot_root` switches `/` to the rootfs while detaching the host's `/oldroot`.
3. **Execution**: The command executes strictly within the new root filesystem context.

---

## Step 2: Unprivileged / Rootless Mode (`--rootless`)

To demonstrate user namespace isolation without requiring `root` or `sudo`:

```bash
# Execute isolated in a fresh user namespace with mapped UID/GID
./container --rootless /bin/echo "Running as unprivileged isolated user"
```
*Note: Rootless execution demonstrates user namespace mappings (`CLONE_NEWUSER`), but does not perform privileged `pivot_root`, filesystem mounts, or cgroup resource limits.*

---

## Step 3: Resource Monitoring (cgroups v2)

Run a background workload to demonstrate resource monitoring.

```bash
# In the running container, or by starting another command
./container create cpu-hog --cpu 30 --command "sh -c 'while true; do :; done'"
sudo ./container start cpu-hog

# View stats (reads directly from cgroups v2 hierarchy)
./container stats cpu-hog
```

*Explain during presentation:* "Here we see the CPU limits being enforced by the Linux kernel via cgroups v2. The `monitor.c` module reads these exact values from `/sys/fs/cgroup/lightweight-containers/cpu-hog/cpu.stat`."

## Step 4: Process Isolation (Namespaces)

Demonstrate that the container has an isolated view.

```bash
# Create a container that lists its processes
./container create iso-test --command "ps aux"
sudo ./container start iso-test
```
*Explain during presentation:* "Because we use the `CLONE_NEWPID` flag when calling `clone()`, and we mount a private `/proc` in the new mount namespace, the container only sees its own processes, starting at PID 1."

## Step 5: Cleanup

Show proper lifecycle management.

```bash
# Stop running containers
./container stop demo1
./container stop cpu-hog
./container stop alpine-box

# Remove them
./container remove demo1
./container remove cpu-hog
./container remove iso-test
./container remove alpine-box

# List to confirm they are gone
./container list
```

## Conclusion
This demonstration highlights the foundational technologies (Namespaces for isolation, Cgroups for resource allocation, and `pivot_root` for root filesystem containment) that power modern container engines like Docker.
