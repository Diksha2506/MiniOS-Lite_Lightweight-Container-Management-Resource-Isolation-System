# -*- coding: utf-8 -*-
"""
demo_containers.py
------------------
Dynamically creates up to 6 demonstration containers that showcase different
real-world container use-cases and lifecycle states.

Cases are driven by a config table (easy to extend).  Each row declares:
  - role        : human-readable description
  - name_prefix : container name (must match ^[a-zA-Z0-9_-]+$)
  - command     : workload command
  - memory_mb   : cgroup memory limit  (0 = unlimited)
  - cpu_pct     : cgroup CPU cap       (0 = unlimited)
  - start       : whether to start the container after creation
  - stop_after  : stop it right after starting (demonstrates STOPPED state)
"""

import urllib.request
import json
import sys
import time

BASE_URL = "http://localhost:8000/api/containers"
MAX_CONTAINERS = 6

# ─────────────────────────────────────────────────────────────────────────────
# Demo case definitions (NOT hardcoded values – config-driven)
# ─────────────────────────────────────────────────────────────────────────────
DEMO_CASES = [
    {
        "role":        "Web Server",
        "name_prefix": "web-server",
        "command":     "python3 -m http.server 8080",
        "memory_mb":   512,
        "cpu_pct":     50,
        "start":       True,
        "stop_after":  False,   # stays RUNNING → green badge
        "description": "Simulates a lightweight HTTP server container with 512 MB RAM cap and 50% CPU limit.",
    },
    {
        "role":        "High-Memory DB Cache",
        "name_prefix": "db-cache",
        "command":     "redis-server --save 60 1",
        "memory_mb":   2048,
        "cpu_pct":     30,
        "start":       True,
        "stop_after":  False,   # stays RUNNING → green badge
        "description": "Demonstrates a memory-intensive workload (2 GB limit). Models a Redis-style cache.",
    },
    {
        "role":        "CPU-Intensive Cruncher",
        "name_prefix": "cpu-cruncher",
        "command":     "python3 -c \"while True: pass\"",
        "memory_mb":   256,
        "cpu_pct":     90,
        "start":       True,
        "stop_after":  False,   # stays RUNNING → shows high CPU cap
        "description": "Demonstrates cgroup CPU throttling. The container is capped at 90% CPU.",
    },
    {
        "role":        "Idle / Minimal Worker",
        "name_prefix": "idle-worker",
        "command":     "sleep 86400",
        "memory_mb":   64,
        "cpu_pct":     5,
        "start":       True,
        "stop_after":  False,   # RUNNING but consuming nothing
        "description": "A near-zero-resource background job. Shows that cgroup limits can be set very low.",
    },
    {
        "role":        "Stopped / Exited App",
        "name_prefix": "stopped-app",
        "command":     "echo 'job finished'",
        "memory_mb":   128,
        "cpu_pct":     10,
        "start":       True,
        "stop_after":  True,    # started then stopped → STOPPED state
        "description": "Container is started and then stopped immediately, demonstrating the STOPPED lifecycle state.",
    },
    {
        "role":        "Monitoring Agent",
        "name_prefix": "monitor-agent",
        "command":     "top -b -d 5",
        "memory_mb":   256,
        "cpu_pct":     20,
        "start":       True,
        "stop_after":  False,   # RUNNING – models an ops/metrics agent
        "description": "Models an observability sidecar that runs alongside workloads to collect metrics.",
    },
]

# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

def _request(method: str, path: str, payload: dict | None = None) -> dict:
    url = BASE_URL + path
    data = json.dumps(payload).encode() if payload else None
    headers = {"Content-Type": "application/json"} if data else {}
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return {"ok": True, "body": json.loads(resp.read())}
    except urllib.error.HTTPError as exc:
        body = exc.read().decode(errors="ignore")
        return {"ok": False, "body": body, "status": exc.code}
    except Exception as exc:
        return {"ok": False, "body": str(exc)}


def fetch_existing() -> list[str]:
    """Return names of currently registered containers."""
    res = _request("GET", "")
    if not res["ok"]:
        return []
    return [c["name"] for c in res["body"]]


def cleanup_old(names: list[str]) -> None:
    """Stop-then-delete containers whose names match demo prefixes."""
    prefixes = {c["name_prefix"] for c in DEMO_CASES}
    for name in names:
        if any(name.startswith(p) for p in prefixes):
            _request("POST", f"/{name}/stop")   # safe to call even if already stopped
            _request("DELETE", f"/{name}")
            print(f"  [X] Removed old container: {name}")


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main() -> None:
    cases = DEMO_CASES[:MAX_CONTAINERS]

    print("=" * 65)
    print("  Mini-Docker Demo Container Setup")
    print("=" * 65)

    # 1. Cleanup any leftover demo containers
    print("\n[1/3] Cleaning up old demo containers …")
    existing = fetch_existing()
    cleanup_old(existing)

    # 2. Create containers dynamically from config
    print(f"\n[2/3] Creating {len(cases)} demonstration containers …\n")
    created = []
    for case in cases:
        name = case["name_prefix"]
        payload = {
            "name":      name,
            "command":   case["command"],
            "memory_mb": case["memory_mb"],
            "cpu_pct":   case["cpu_pct"],
        }
        res = _request("POST", "", payload)
        if not res["ok"]:
            print(f"  [FAIL] [{case['role']}] Create failed: {res['body']}")
            continue
        print(f"  [OK]   [{case['role']}] '{name}' created  "
              f"(mem={case['memory_mb']} MB, cpu<={case['cpu_pct']}%)")
        created.append(case)

    # 3. Apply lifecycle transitions
    print(f"\n[3/3] Applying lifecycle states …\n")
    for case in created:
        name = case["name_prefix"]
        if case["start"]:
            res = _request("POST", f"/{name}/start")
            if res["ok"]:
                print(f"  [RUN]  '{name}' -> RUNNING")
            else:
                print(f"  [FAIL] '{name}' start failed: {res['body']}")

            if case["stop_after"]:
                time.sleep(0.3)   # brief pause so PID is visible
                res = _request("POST", f"/{name}/stop")
                if res["ok"]:
                    print(f"  [STOP] '{name}' -> STOPPED  (demonstrates exited state)")
                else:
                    print(f"  [FAIL] '{name}' stop failed: {res['body']}")

    # Summary
    print("\n" + "=" * 65)
    print("  Final Container States")
    print("=" * 65)
    final = _request("GET", "")
    if final["ok"]:
        fmt = "  {:<22} {:>6}   {:<10} {:>10} MB  {:>6}%"
        print(fmt.format("NAME", "PID", "STATE", "MEM LIMIT", "CPU CAP"))
        print("  " + "-" * 61)
        for c in final["body"]:
            print(fmt.format(
                c["name"], c["pid"], c["state"],
                c["mem_limit"], c["cpu_limit"]
            ))
    print("\nDone! Open http://localhost:5173 to view the dashboard.\n")


if __name__ == "__main__":
    main()
