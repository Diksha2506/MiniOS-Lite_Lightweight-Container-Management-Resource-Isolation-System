#!/bin/bash
# MiniOS cgroup setup — run with sudo after each WSL restart
# Usage: sudo bash scripts/setup_cgroups.sh
set -e

CGROUP_DIR="/sys/fs/cgroup/lightweight-containers"

echo "[setup_cgroups] Creating cgroup directory at $CGROUP_DIR ..."
mkdir -p "$CGROUP_DIR"

echo "[setup_cgroups] Enabling cpu, memory, pids controllers ..."
echo "+cpu +memory +pids" > "$CGROUP_DIR/cgroup.subtree_control"

echo "[setup_cgroups] Verifying ..."
CTRL=$(cat "$CGROUP_DIR/cgroup.subtree_control")
echo "[setup_cgroups] subtree_control = $CTRL"

echo "[setup_cgroups] Done. MiniOS cgroup controllers are ready."
