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

ALLOWED_SUBCOMMANDS = {"list", "create", "start", "stop", "remove", "stats", "inspect-ns", "inspect-net"}


# Privileged runtime operations:
#   - start: clone() namespaces, pivot_root, cgroup attach (CAP_SYS_ADMIN)
#   - stop: kill root-owned container PIDs started via sudo
# create/list/stats/remove remain unprivileged (metadata + cgroup reads).
_SUDO_REQUIRED_SUBCOMMANDS = {"start", "stop"}


def _run(*args: str) -> dict[str, Any]:
    """
    Run the C engine with the supplied arguments.
    Only whitelisted sub-commands may be executed.
    'start' and 'stop' are wrapped with sudo for namespace/cgroup privileges
    and for signaling root-owned container processes. Other commands run as the user.
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

    # Only prefix with sudo for the subcommands that actually need root.
    if args[0] in _SUDO_REQUIRED_SUBCOMMANDS:
        cmd = ["sudo", "-n", str(ENGINE)] + list(args)
    else:
        cmd = [str(ENGINE)] + list(args)

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
# Sudo availability probe                                                       #
# --------------------------------------------------------------------------- #

def _check_sudo_nopasswd() -> bool:
    """
    Return True iff `sudo -n true` exits 0, i.e. passwordless sudo is
    configured for the current user.  Used only in system_info() to expose
    the honest execution mode to the dashboard.  Never raises.
    """
    try:
        r = subprocess.run(
            ["sudo", "-n", "true"],
            capture_output=True,
            timeout=3,
        )
        return r.returncode == 0
    except Exception:
        return False


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
        rootfs = None
        i = 2
        while i < len(args):
            if args[i] == "--memory" and i + 1 < len(args):
                mem = int(args[i + 1]); i += 2
            elif args[i] == "--cpu" and i + 1 < len(args):
                cpu = int(args[i + 1]); i += 2
            elif args[i] == "--command" and i + 1 < len(args):
                command = args[i + 1]; i += 2
            elif args[i] == "--rootfs" and i + 1 < len(args):
                rootfs = args[i + 1]; i += 2
            else:
                i += 1
        _MOCK_CONTAINERS[name] = {
            "pid": -1,
            "state": "STOPPED",
            "mem_limit": mem,
            "cpu_limit": cpu,
            "command": command,
            "rootfs": rootfs,
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

    if cmd == "inspect-ns":
        name = args[1]
        if name not in _MOCK_CONTAINERS:
            return {"ok": False, "stdout": "", "stderr": f"Container {name} not found."}
        c = _MOCK_CONTAINERS[name]
        pid = c["pid"] if c["pid"] > 0 else 0
        import json
        is_running = c["state"] == "RUNNING"
        mock_ns = [
            {"type": "pid", "available": is_running, "inode": 4026532201 if is_running else 0, "path": f"/proc/{pid}/ns/pid" if is_running else ""},
            {"type": "uts", "available": is_running, "inode": 4026532202 if is_running else 0, "path": f"/proc/{pid}/ns/uts" if is_running else ""},
            {"type": "mnt", "available": is_running, "inode": 4026532203 if is_running else 0, "path": f"/proc/{pid}/ns/mnt" if is_running else ""},
            {"type": "ipc", "available": is_running, "inode": 4026532204 if is_running else 0, "path": f"/proc/{pid}/ns/ipc" if is_running else ""},
            {"type": "net", "available": is_running, "inode": 4026532205 if is_running else 0, "path": f"/proc/{pid}/ns/net" if is_running else ""},
            {"type": "user", "available": is_running, "inode": 4026531837 if is_running else 0, "path": f"/proc/{pid}/ns/user" if is_running else ""},
        ]
        return {
            "ok": True,
            "stdout": json.dumps({"container": name, "pid": pid, "namespaces": mock_ns, "mock": True}) + "\n",
            "stderr": "",
        }

    if cmd == "inspect-net":
        name = args[1]
        if name not in _MOCK_CONTAINERS:
            return {"ok": False, "stdout": "", "stderr": f"Container {name} not found."}
        c = _MOCK_CONTAINERS[name]
        import json
        is_running = c["state"] == "RUNNING"
        net_connected = c.get("net_connected", is_running)
        net_info = {
            "container": name,
            "connected": net_connected,
            "ip_address": "172.20.0.2" if net_connected else "",
            "gateway": "172.20.0.1" if net_connected else "",
            "bridge": "minios-br0" if net_connected else "",
            "host_interface": "vh_mock0" if net_connected else "",
            "container_interface": "eth0" if net_connected else "",
            "pid": c["pid"],
            "mock": True,
        }
        return {
            "ok": True,
            "stdout": json.dumps(net_info) + "\n",
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
            name = parts[0]
            entry = {
                "name": name,
                "pid": int(parts[1]),
                "state": parts[2],
                "mem_limit": int(parts[3]),
                "cpu_limit": int(parts[4]),
            }
            # Mock mode may retain optional rootfs; Linux list CLI does not print it yet.
            mock = _MOCK_CONTAINERS.get(name)
            if mock and mock.get("rootfs"):
                entry["rootfs"] = mock["rootfs"]
            containers.append(entry)
    return containers


def create_container(name: str, command: str, memory: int, cpu: int, rootfs: str | None = None) -> dict:
    args = ["create", name]
    if memory > 0:
        args += ["--memory", str(memory)]
    if cpu > 0:
        args += ["--cpu", str(cpu)]
    args += ["--command", command]
    if rootfs:
        args += ["--rootfs", rootfs]
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


def inspect_namespaces(name: str) -> dict[str, Any]:
    res = _run("inspect-ns", name, "--json")
    if not res["ok"]:
        return {"container": name, "pid": -1, "namespaces": [], "error": res["stderr"]}
    import json
    try:
        data = json.loads(res["stdout"])
        if not IS_LINUX:
            data["mock"] = True
        return data
    except Exception as exc:
        return {"container": name, "pid": -1, "namespaces": [], "error": str(exc)}


def inspect_network(name: str) -> dict[str, Any]:
    res = _run("inspect-net", name, "--json")
    if not res["ok"]:
        return {
            "container": name,
            "connected": False,
            "ip_address": "",
            "gateway": "",
            "bridge": "",
            "host_interface": "",
            "container_interface": "",
            "pid": -1,
            "error": res["stderr"],
        }
    import json
    try:
        data = json.loads(res["stdout"])
        if not IS_LINUX:
            data["mock"] = True
        return data
    except Exception as exc:
        return {
            "container": name,
            "connected": False,
            "ip_address": "",
            "gateway": "",
            "bridge": "",
            "host_interface": "",
            "container_interface": "",
            "pid": -1,
            "error": str(exc),
        }


def connect_network(name: str) -> dict[str, Any]:
    if not IS_LINUX:
        if name in _MOCK_CONTAINERS:
            _MOCK_CONTAINERS[name]["net_connected"] = True
            return {"ok": True, "message": f"Container {name} connected to network (simulated)."}
        return {"ok": False, "message": f"Container {name} not found."}
    # In Linux, inspect-net verifies existing bridge attachment
    net = inspect_network(name)
    if net.get("connected"):
        return {"ok": True, "message": f"Container {name} is connected to bridge {net.get('bridge')}."}
    return {"ok": False, "message": f"Container {name} is not running or network not active."}


def disconnect_network(name: str) -> dict[str, Any]:
    if not IS_LINUX:
        if name in _MOCK_CONTAINERS:
            _MOCK_CONTAINERS[name]["net_connected"] = False
            return {"ok": True, "message": f"Container {name} disconnected from network (simulated)."}
        return {"ok": False, "message": f"Container {name} not found."}
    return {"ok": True, "message": f"Container {name} network managed via container lifecycle."}


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
        # mock_mode is True on non-Linux OR when the binary is absent on Linux.
        # This is set honestly: IS_LINUX and engine present means real mode for
        # list/create/stop/remove/stats. 'start' additionally needs passwordless sudo.
        "mock_mode": not IS_LINUX or (IS_LINUX and not ENGINE.exists()),
        "sudo_available": _check_sudo_nopasswd() if IS_LINUX else False,
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
