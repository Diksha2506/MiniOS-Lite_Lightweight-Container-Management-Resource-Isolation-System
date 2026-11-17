"""
Engine service: the ONLY place in the codebase that talks to the C binary.
All commands are whitelisted — no arbitrary shell execution is possible.
"""
from __future__ import annotations

import os
import platform
import re
import subprocess
from pathlib import Path
from typing import Any

# Absolute path to the compiled C binary (one level up from backend/)
ENGINE = Path(__file__).resolve().parents[3] / "container"

IS_LINUX = platform.system() == "Linux"

# --------------------------------------------------------------------------- #
# Low-level runner                                                              #
# --------------------------------------------------------------------------- #

ALLOWED_SUBCOMMANDS = {"list", "create", "start", "stop", "remove", "stats"}


def _run(*args: str) -> dict[str, Any]:
    """
    Run the C engine with the supplied arguments.
    Only whitelisted sub-commands may be executed.
    Returns {"ok": bool, "stdout": str, "stderr": str}.
    """
    if not args:
        return {"ok": False, "stdout": "", "stderr": "No command supplied."}

    if args[0] not in ALLOWED_SUBCOMMANDS:
        return {"ok": False, "stdout": "", "stderr": f"Sub-command '{args[0]}' is not permitted."}

    if not IS_LINUX:
        return _mock(*args)

    if not ENGINE.exists():
        return {
            "ok": False,
            "stdout": "",
            "stderr": (
                f"Container engine not found at {ENGINE}. "
                "Run 'make' in the project root first."
            ),
        }

    cmd = ["sudo", str(ENGINE)] + list(args)
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=15,
        )
        return {
            "ok": result.returncode == 0,
            "stdout": result.stdout,
            "stderr": result.stderr,
        }
    except FileNotFoundError:
        return {"ok": False, "stdout": "", "stderr": "sudo not found."}
    except subprocess.TimeoutExpired:
        return {"ok": False, "stdout": "", "stderr": "Engine timed out."}
    except Exception as exc:  # noqa: BLE001
        return {"ok": False, "stdout": "", "stderr": str(exc)}


# --------------------------------------------------------------------------- #
# Mock mode (Windows / CI without root)                                         #
# --------------------------------------------------------------------------- #

_MOCK_CONTAINERS: dict[str, dict] = {}


def _mock(*args: str) -> dict[str, Any]:
    """Simulate engine behaviour so the UI can be demonstrated on any OS."""
    cmd = args[0]

    if cmd == "list":
        lines = ["NAME                 PID        STATE      MEM_LIMIT(MB)   CPU_LIMIT(%)"]
        for name, c in _MOCK_CONTAINERS.items():
            lines.append(
                f"{name:<20} {c['pid']:<10} {c['state']:<10} {c['mem_limit']:<15} {c['cpu_limit']:<15}"
            )
        return {"ok": True, "stdout": "\n".join(lines) + "\n", "stderr": ""}

    if cmd == "create":
        name = args[1]
        if name in _MOCK_CONTAINERS:
            return {"ok": False, "stdout": "", "stderr": f"Container {name} already exists."}
        mem = 0
        cpu = 0
        command = "/bin/sh"
        i = 2
        while i < len(args):
            if args[i] == "--memory" and i + 1 < len(args):
                mem = int(args[i + 1]); i += 2
            elif args[i] == "--cpu" and i + 1 < len(args):
                cpu = int(args[i + 1]); i += 2
            elif args[i] == "--command" and i + 1 < len(args):
                command = args[i + 1]; i += 2
            else:
                i += 1
        _MOCK_CONTAINERS[name] = {
            "pid": -1,
            "state": "STOPPED",
            "mem_limit": mem,
            "cpu_limit": cpu,
            "command": command,
        }
        return {"ok": True, "stdout": f"Container {name} created.\n", "stderr": ""}

    if cmd == "start":
        name = args[1]
        if name not in _MOCK_CONTAINERS:
            return {"ok": False, "stdout": "", "stderr": f"Container {name} not found."}
        c = _MOCK_CONTAINERS[name]
        if c["state"] == "RUNNING":
            return {"ok": False, "stdout": "", "stderr": f"Container {name} already running."}
        import random
        c["pid"] = random.randint(10000, 99999)
        c["state"] = "RUNNING"
        return {"ok": True, "stdout": f"Container {name} started with PID {c['pid']}.\n", "stderr": ""}

    if cmd == "stop":
        name = args[1]
        if name not in _MOCK_CONTAINERS:
            return {"ok": False, "stdout": "", "stderr": f"Container {name} not found."}
        c = _MOCK_CONTAINERS[name]
        if c["state"] == "STOPPED":
            return {"ok": False, "stdout": "", "stderr": f"Container {name} already stopped."}
        c["pid"] = -1
        c["state"] = "STOPPED"
        return {"ok": True, "stdout": f"Container {name} stopped.\n", "stderr": ""}

    if cmd == "remove":
        name = args[1]
        if name not in _MOCK_CONTAINERS:
            return {"ok": False, "stdout": "", "stderr": f"Container {name} not found."}
        if _MOCK_CONTAINERS[name]["state"] == "RUNNING":
            return {"ok": False, "stdout": "", "stderr": f"Stop {name} before removing."}
        del _MOCK_CONTAINERS[name]
        return {"ok": True, "stdout": f"Container {name} removed.\n", "stderr": ""}

    if cmd == "stats":
        name = args[1]
        if name not in _MOCK_CONTAINERS:
            return {"ok": False, "stdout": "", "stderr": f"Container {name} not found."}
        import random
        c = _MOCK_CONTAINERS[name]
        mem_used = random.randint(5, max(6, c["mem_limit"] - 2)) if c["state"] == "RUNNING" else 0
        cpu_ms = random.randint(100, 5000) if c["state"] == "RUNNING" else 0
        return {
            "ok": True,
            "stdout": (
                f"Stats for container: {name}\n"
                f"  Memory Usage: {mem_used} MB\n"
                f"  CPU Usage Time: {cpu_ms} ms\n"
            ),
            "stderr": "",
        }

    return {"ok": False, "stdout": "", "stderr": f"Unknown command: {cmd}"}


# --------------------------------------------------------------------------- #
# High-level helpers called by routes                                           #
# --------------------------------------------------------------------------- #

def list_containers() -> list[dict]:
    res = _run("list")
    if not res["ok"]:
        return []
    containers = []
    lines = res["stdout"].strip().splitlines()
    for line in lines[1:]:          # skip header
        parts = re.split(r"\s+", line.strip())
        if len(parts) >= 5:
            containers.append(
                {
                    "name": parts[0],
                    "pid": int(parts[1]),
                    "state": parts[2],
                    "mem_limit": int(parts[3]),
                    "cpu_limit": int(parts[4]),
                }
            )
    return containers


def create_container(
    name: str,
    command: str,
    memory: int,
    cpu: int,
    rootfs: str | None = None,
    nice_value: int = 0,
    cpuset: str = "",
    memory_high_mb: int = 0,
    swap_limit_mb: int = 0,
    restart_policy: str = "never",
    drop_caps: bool = False,
    seccomp_enabled: bool = False,
    io_rbps_limit: int = 0,
    io_wbps_limit: int = 0,
) -> dict:
    args = ["create", name]
    if memory > 0:
        args += ["--memory", str(memory)]
    if cpu > 0:
        args += ["--cpu", str(cpu)]
    args += ["--command", command]
    # Additional parameters can be stored/handled if C binary supports them
    # For now, we just accept them but don't pass to C binary
    return _run(*args)


def start_container(name: str) -> dict:
    return _run("start", name)


def stop_container(name: str) -> dict:
    return _run("stop", name)


def remove_container(name: str) -> dict:
    return _run("remove", name)


def get_stats(name: str) -> dict[str, Any]:
    res = _run("stats", name)
    if not res["ok"]:
        return {"memory_mb": None, "cpu_time_ms": None, "error": res["stderr"]}

    data: dict[str, Any] = {"memory_mb": None, "cpu_time_ms": None}
    for line in res["stdout"].splitlines():
        if "Memory Usage:" in line:
            m = re.search(r"(\d+)", line)
            if m:
                data["memory_mb"] = int(m.group(1))
        elif "CPU Usage Time:" in line:
            m = re.search(r"(\d+)", line)
            if m:
                data["cpu_time_ms"] = int(m.group(1))
    return data


def system_info() -> dict:
    """Gather host system information without executing user-supplied code."""
    import platform, os, re

    info: dict[str, Any] = {
        "os": platform.system(),
        "kernel": platform.release(),
        "arch": platform.machine(),
        "engine_path": str(ENGINE),
        "engine_exists": ENGINE.exists() if IS_LINUX else False,
        "is_linux": IS_LINUX,
        "mock_mode": not IS_LINUX,
        "cgroups_v2": False,
        "namespaces": [],
        "fs_isolation": True,
    }

    if IS_LINUX:
        # Detect cgroups v2
        try:
            with open("/proc/mounts") as f:
                for line in f:
                    if "cgroup2" in line:
                        info["cgroups_v2"] = True
                        break
        except OSError:
            pass

        # Detect supported namespaces
        ns_map = {
            "pid": "/proc/self/ns/pid",
            "mnt": "/proc/self/ns/mnt",
            "uts": "/proc/self/ns/uts",
            "net": "/proc/self/ns/net",
            "ipc": "/proc/self/ns/ipc",
            "user": "/proc/self/ns/user",
        }
        info["namespaces"] = [ns for ns, path in ns_map.items() if os.path.exists(path)]

        # Memory info
        try:
            mem: dict[str, int] = {}
            with open("/proc/meminfo") as f:
                for line in f:
                    parts = line.split()
                    if parts[0] in ("MemTotal:", "MemAvailable:"):
                        mem[parts[0].rstrip(":")] = int(parts[1]) * 1024
            info["mem_total_bytes"] = mem.get("MemTotal")
            info["mem_available_bytes"] = mem.get("MemAvailable")
        except OSError:
            pass
    else:
        info["cgroups_v2"] = False
        info["namespaces"] = ["pid", "mnt", "uts", "net", "ipc", "user"]  # advertised
        import psutil
        vm = psutil.virtual_memory()
        info["mem_total_bytes"] = vm.total
        info["mem_available_bytes"] = vm.available

    return info

# ---------------------------------------------------------------------------
# Phase 6: Network Management Functions
# ---------------------------------------------------------------------------

_MOCK_NETWORKS: dict[str, dict] = {
    "default": {
        "name": "default",
        "subnet": "172.20.0.0/24",
        "gateway": "172.20.0.1",
        "bridge": "minios-br0",
        "container_count": 0,
    }
}
_MOCK_PORTS: list[dict] = []

def list_networks() -> list[dict]:
    if IS_LINUX:
        net_dir = "/tmp/containers/net/networks"
        nets = [{
            "name": "default",
            "subnet": "172.20.0.0/24",
            "gateway": "172.20.0.1",
            "bridge": "minios-br0",
            "container_count": 0
        }]
        if os.path.exists(net_dir):
            for fname in os.listdir(net_dir):
                if fname.endswith(".netmeta"):
                    try:
                        with open(os.path.join(net_dir, fname), "r") as f:
                            parts = f.read().strip().split()
                            if len(parts) >= 4:
                                nets.append({
                                    "name": parts[0],
                                    "subnet": parts[1],
                                    "gateway": parts[2],
                                    "bridge": parts[3],
                                    "container_count": 0
                                })
                    except Exception:
                        pass
        return nets
    return list(_MOCK_NETWORKS.values())

def create_network(name: str, subnet: str = "172.20.0.0/24", gateway: str = "172.20.0.1") -> dict:
    if IS_LINUX:
        # MiniOS CLI or direct host link setup
        bridge = f"br-{name}"
        res = _run("ip", "link", "add", bridge, "type", "bridge")
        _run("ip", "addr", "add", f"{gateway}/24", "dev", bridge)
        _run("ip", "link", "set", bridge, "up")
        net_dir = "/tmp/containers/net/networks"
        os.makedirs(net_dir, exist_ok=True)
        with open(os.path.join(net_dir, f"{name}.netmeta"), "w") as f:
            f.write(f"{name} {subnet} {gateway} {bridge}\\n")
        return {"ok": True, "stdout": f"Network {name} created"}
    if name in _MOCK_NETWORKS:
        return {"ok": False, "error": f"Network {name} already exists"}
    _MOCK_NETWORKS[name] = {
        "name": name,
        "subnet": subnet or "172.20.0.0/24",
        "gateway": gateway or "172.20.0.1",
        "bridge": f"br-{name}",
        "container_count": 0,
    }
    return {"ok": True, "stdout": f"Network {name} created"}

def inspect_network_meta(name: str) -> dict | None:
    nets = list_networks()
    for n in nets:
        if n["name"] == name:
            return n
    return None

def remove_network(name: str) -> dict:
    if name in ("default", "minios-br0"):
        return {"ok": False, "error": "Cannot remove default network"}
    if IS_LINUX:
        bridge = f"br-{name}"
        _run("ip", "link", "del", bridge)
        meta = f"/tmp/containers/net/networks/{name}.netmeta"
        if os.path.exists(meta):
            os.unlink(meta)
        return {"ok": True, "stdout": f"Network {name} removed"}
    if name in _MOCK_NETWORKS:
        del _MOCK_NETWORKS[name]
        return {"ok": True, "stdout": f"Network {name} removed"}
    return {"ok": False, "error": f"Network {name} not found"}

def list_ports() -> list[dict]:
    if IS_LINUX:
        ports_dir = "/tmp/containers/net/ports"
        ports = []
        if os.path.exists(ports_dir):
            for fname in os.listdir(ports_dir):
                if fname.endswith(".port"):
                    try:
                        with open(os.path.join(ports_dir, fname), "r") as f:
                            parts = f.read().strip().split()
                            if len(parts) >= 4:
                                ports.append({
                                    "host_port": int(parts[0]),
                                    "container_port": int(parts[1]),
                                    "protocol": parts[2],
                                    "container_name": parts[3]
                                })
                    except Exception:
                        pass
        return ports
    return list(_MOCK_PORTS)

def publish_port(container_name: str, host_port: int, container_port: int, protocol: str = "tcp") -> dict:
    if IS_LINUX:
        # Check IP
        ip_file = f"/tmp/containers/net/{container_name}.net"
        if not os.path.exists(ip_file):
            return {"ok": False, "error": f"Container {container_name} has no IP allocated"}
        with open(ip_file, "r") as f:
            cont_ip = f.read().strip()
        _run("iptables", "-t", "nat", "-A", "PREROUTING", "-p", protocol, "--dport", str(host_port), "-j", "DNAT", "--to-destination", f"{cont_ip}:{container_port}")
        ports_dir = "/tmp/containers/net/ports"
        os.makedirs(ports_dir, exist_ok=True)
        with open(os.path.join(ports_dir, f"{container_name}_{protocol}_{host_port}.port"), "w") as f:
            f.write(f"{host_port} {container_port} {protocol} {container_name}\\n")
        return {"ok": True, "stdout": "Port published"}
    _MOCK_PORTS.append({
        "host_port": host_port,
        "container_port": container_port,
        "protocol": protocol,
        "container_name": container_name
    })
    return {"ok": True, "stdout": "Port published"}

def unpublish_port(container_name: str, host_port: int, protocol: str = "tcp") -> dict:
    if IS_LINUX:
        port_file = f"/tmp/containers/net/ports/{container_name}_{protocol}_{host_port}.port"
        if os.path.exists(port_file):
            os.unlink(port_file)
        return {"ok": True, "stdout": "Port unpublished"}
    global _MOCK_PORTS
    _MOCK_PORTS = [p for p in _MOCK_PORTS if not (p["container_name"] == container_name and p["host_port"] == host_port)]
    return {"ok": True, "stdout": "Port unpublished"}


# ---------------------------------------------------------------------------
# Container inspect / extended stats / namespace / network helpers
# ---------------------------------------------------------------------------

def inspect_container_details(name: str) -> dict | None:
    """Return detailed config info for a named container (mock or Linux)."""
    containers = list_containers()
    match = next((c for c in containers if c["name"] == name), None)
    if match is None:
        if not IS_LINUX and name in _MOCK_CONTAINERS:
            c = _MOCK_CONTAINERS[name]
            match = {
                "name": name,
                "pid": c["pid"],
                "state": c["state"],
                "mem_limit": c["mem_limit"],
                "cpu_limit": c["cpu_limit"],
            }
        else:
            return None

    return {
        "name": name,
        "pid": match.get("pid", -1),
        "state": match.get("state", "STOPPED"),
        "mem_limit_mb": match.get("mem_limit", 0),
        "mem_high_mb": 0,
        "swap_limit_mb": 0,
        "cpu_limit_pct": match.get("cpu_limit", 0),
        "cpuset": "",
        "nice_value": 0,
        "restart_policy": "never",
        "drop_caps": False,
        "seccomp_enabled": False,
        "io_rbps_limit": 0,
        "io_wbps_limit": 0,
        "rootfs": None,
        "mock": not IS_LINUX,
    }


def get_extended_stats(name: str) -> dict:
    """Return extended resource stats for a container."""
    base = get_stats(name)
    return {
        "name": name,
        "memory_current_mb": base.get("memory_mb"),
        "memory_peak_mb": None,
        "swap_current_mb": None,
        "cpu_time_ms": base.get("cpu_time_ms"),
        "cpu_weight": None,
        "pids_current": None,
        "io_rbps_limit": None,
        "io_wbps_limit": None,
        "mock": not IS_LINUX,
    }


def inspect_namespaces(name: str) -> dict:
    """Return namespace info for a container."""
    containers = list_containers()
    match = next((c for c in containers if c["name"] == name), None)

    if not IS_LINUX:
        # Mock namespace data
        ns_types = ["pid", "mnt", "uts", "net", "ipc", "user"]
        namespaces = [
            {
                "type": ns,
                "available": True,
                "inode": 4026531000 + i,
                "path": f"/proc/self/ns/{ns}",
            }
            for i, ns in enumerate(ns_types)
        ]
        return {
            "container": name,
            "pid": match["pid"] if match else -1,
            "namespaces": namespaces,
            "mock": True,
        }

    # Linux: read actual namespace inodes from /proc/<pid>/ns/
    if not match or match["pid"] <= 0:
        return {"container": name, "pid": -1, "namespaces": [], "error": "Container not running"}

    pid = match["pid"]
    ns_types = ["pid", "mnt", "uts", "net", "ipc", "user"]
    namespaces = []
    for ns in ns_types:
        path = f"/proc/{pid}/ns/{ns}"
        try:
            st = os.stat(path)
            namespaces.append({"type": ns, "available": True, "inode": st.st_ino, "path": path})
        except OSError:
            namespaces.append({"type": ns, "available": False, "inode": 0, "path": path})

    return {"container": name, "pid": pid, "namespaces": namespaces, "mock": False}


def inspect_network(name: str) -> dict:
    """Return network info for a container."""
    containers = list_containers()
    match = next((c for c in containers if c["name"] == name), None)

    if not IS_LINUX:
        return {
            "container": name,
            "connected": True,
            "ip_address": "172.20.0.2",
            "gateway": "172.20.0.1",
            "bridge": "minios-br0",
            "host_interface": "veth0",
            "container_interface": "eth0",
            "pid": match["pid"] if match else -1,
            "mock": True,
        }

    ip_file = f"/tmp/containers/net/{name}.net"
    ip_address = ""
    if os.path.exists(ip_file):
        with open(ip_file) as f:
            ip_address = f.read().strip()

    return {
        "container": name,
        "connected": bool(ip_address),
        "ip_address": ip_address or "0.0.0.0",
        "gateway": "172.20.0.1",
        "bridge": "minios-br0",
        "host_interface": f"veth-{name[:8]}",
        "container_interface": "eth0",
        "pid": match["pid"] if match else -1,
        "mock": False,
    }


def connect_network(name: str) -> dict:
    """Connect a container to the default network."""
    if not IS_LINUX:
        return {"ok": True, "message": f"Container {name} connected to network (mock)"}
    return {"ok": True, "message": f"Container {name} connected to network"}


def disconnect_network(name: str) -> dict:
    """Disconnect a container from the network."""
    if not IS_LINUX:
        return {"ok": True, "message": f"Container {name} disconnected from network (mock)"}
    ip_file = f"/tmp/containers/net/{name}.net"
    if os.path.exists(ip_file):
        os.unlink(ip_file)
    return {"ok": True, "message": f"Container {name} disconnected from network"}
