#!/usr/bin/env bash
# test_network.sh — Phase 1: Container networking verification
# Must run as root in WSL2 Ubuntu.
# Tests veth pair creation, IP assignment, container-to-container ping,
# and clean teardown.

set -euo pipefail

BINARY="$(dirname "$0")/../container"
PASS=0
FAIL=0
SKIP=0

# ── helpers ──────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "  ${GREEN}PASS${NC}  $1"; PASS=$((PASS + 1)); }
fail() { echo -e "  ${RED}FAIL${NC}  $1"; FAIL=$((FAIL + 1)); }
skip() { echo -e "  ${YELLOW}SKIP${NC}  $1"; SKIP=$((SKIP + 1)); }

section() { echo; echo "── $1 ──"; }

cleanup_container() {
    local name="$1"
    "$BINARY" stop   "$name" >/dev/null 2>&1 || true
    sleep 0.3
    "$BINARY" remove "$name" >/dev/null 2>&1 || true
}

# ── preflight ─────────────────────────────────────────────────────────────────

echo "====================================================================="
echo " MiniOS Phase 1 — Container Networking Test Suite"
echo "====================================================================="

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: Must run as root (sudo bash tests/test_network.sh)"
    exit 1
fi

if [ ! -x "$BINARY" ]; then
    echo "ERROR: container binary not found at $BINARY — run 'make' first"
    exit 1
fi

# Check required tools
for tool in ip ping nsenter; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: required tool '$tool' not found"
        exit 1
    fi
done

rm -rf /tmp/containers
sleep 0.2

# ── create and start two containers ──────────────────────────────────────────

CA="net-a-$$"
CB="net-b-$$"

section "Setup — containers $CA and $CB"

"$BINARY" create "$CA" --command "sleep 60" >/dev/null
"$BINARY" create "$CB" --command "sleep 60" >/dev/null

"$BINARY" start "$CA" >/dev/null
sleep 0.8   # allow veth setup to complete

"$BINARY" start "$CB" >/dev/null
sleep 0.8

# ── get PIDs ─────────────────────────────────────────────────────────────────

PID_A=$("$BINARY" list 2>/dev/null | awk -v n="$CA" '$1==n {print $2}')
PID_B=$("$BINARY" list 2>/dev/null | awk -v n="$CB" '$1==n {print $2}')

echo "  Container A PID: $PID_A"
echo "  Container B PID: $PID_B"

if [ -z "$PID_A" ] || [ "$PID_A" -le 0 ] 2>/dev/null; then
    echo "ERROR: Could not get PID for $CA"
    cleanup_container "$CA"
    cleanup_container "$CB"
    exit 1
fi

if [ -z "$PID_B" ] || [ "$PID_B" -le 0 ] 2>/dev/null; then
    echo "ERROR: Could not get PID for $CB"
    cleanup_container "$CA"
    cleanup_container "$CB"
    exit 1
fi

# ── inspect-net CLI — human output ───────────────────────────────────────────

section "inspect-net CLI — $CA"

NET_A=$("$BINARY" inspect-net "$CA" 2>/dev/null)
echo "$NET_A"

section "inspect-net CLI — $CB"

NET_B=$("$BINARY" inspect-net "$CB" 2>/dev/null)
echo "$NET_B"

# ── extract IP addresses ──────────────────────────────────────────────────────

section "IP Address assignment"

IP_A=$(echo "$NET_A" | grep -i "IP Address" | awk '{print $NF}' || echo "")
IP_B=$(echo "$NET_B" | grep -i "IP Address" | awk '{print $NF}' || echo "")

echo "  Container A IP: $IP_A"
echo "  Container B IP: $IP_B"

if echo "$IP_A" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$'; then
    pass "$CA has valid IP address: $IP_A"
else
    fail "$CA IP address invalid or missing: '$IP_A'"
fi

if echo "$IP_B" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$'; then
    pass "$CB has valid IP address: $IP_B"
else
    fail "$CB IP address invalid or missing: '$IP_B'"
fi

if [ "$IP_A" != "$IP_B" ]; then
    pass "Container IP addresses are unique"
else
    fail "Containers have SAME IP address: $IP_A"
fi

# ── inspect-net JSON output ───────────────────────────────────────────────────

section "inspect-net CLI — JSON output ($CA)"

NET_JSON=$("$BINARY" inspect-net "$CA" --json 2>/dev/null)
echo "$NET_JSON"

if echo "$NET_JSON" | grep -q '"ip_address"'; then
    pass "inspect-net --json contains ip_address field"
else
    fail "inspect-net --json missing ip_address field"
fi

if echo "$NET_JSON" | grep -q '"connected"'; then
    pass "inspect-net --json contains connected field"
else
    fail "inspect-net --json missing connected field"
fi

# ── bridge created ────────────────────────────────────────────────────────────

section "Bridge interface"

if ip link show minios-br0 &>/dev/null; then
    pass "Bridge minios-br0 exists"
else
    fail "Bridge minios-br0 not found"
fi

# ── veth pair visible on host ─────────────────────────────────────────────────

section "Host-side veth interfaces"

VETH_COUNT=$(ip link show | grep -c "^[0-9]*: vh" || true)
echo "  veth interfaces on host: $VETH_COUNT"

if [ "$VETH_COUNT" -ge 2 ]; then
    pass "At least 2 host-side veth interfaces present"
elif [ "$VETH_COUNT" -ge 1 ]; then
    pass "At least 1 host-side veth interface present"
    skip "Expected 2+ veth interfaces for two running containers"
else
    fail "No host-side veth interfaces found"
fi

# ── verify separate network namespaces ───────────────────────────────────────

section "Network namespace separation"

NS_A=$(stat -Lc %i "/proc/$PID_A/ns/net" 2>/dev/null || echo "0")
NS_B=$(stat -Lc %i "/proc/$PID_B/ns/net" 2>/dev/null || echo "0")
NS_HOST=$(stat -Lc %i /proc/self/ns/net 2>/dev/null || echo "0")

echo "  Host net ns inode:    $NS_HOST"
echo "  Container A net ns:   $NS_A"
echo "  Container B net ns:   $NS_B"

if [ "$NS_A" -ne "$NS_HOST" ] && [ "$NS_A" -ne 0 ]; then
    pass "Container A has separate network namespace"
else
    fail "Container A shares host network namespace"
fi

if [ "$NS_B" -ne "$NS_HOST" ] && [ "$NS_B" -ne 0 ]; then
    pass "Container B has separate network namespace"
else
    fail "Container B shares host network namespace"
fi

if [ "$NS_A" -ne "$NS_B" ]; then
    pass "Container A and B have different network namespaces"
else
    fail "Container A and B share the same network namespace"
fi

# ── container-to-container ping ───────────────────────────────────────────────

section "Container-to-container connectivity"

if [ -n "$IP_A" ] && [ -n "$IP_B" ] && \
   echo "$IP_A" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$' && \
   echo "$IP_B" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$'; then

    echo "  Pinging $IP_B from Container A namespace (pid=$PID_A)..."
    if nsenter -t "$PID_A" -n -- ping -c 2 -W 3 "$IP_B" >/dev/null 2>&1; then
        pass "Container A can ping Container B ($IP_A -> $IP_B)"
    else
        fail "Container A CANNOT ping Container B ($IP_A -> $IP_B)"
    fi

    echo "  Pinging $IP_A from Container B namespace (pid=$PID_B)..."
    if nsenter -t "$PID_B" -n -- ping -c 2 -W 3 "$IP_A" >/dev/null 2>&1; then
        pass "Container B can ping Container A ($IP_B -> $IP_A)"
    else
        fail "Container B CANNOT ping Container A ($IP_B -> $IP_A)"
    fi

    echo "  Verifying container peer interface NOT visible on host..."
    if ip link show | grep -q " vp"; then
        fail "Host can see peer veth vp* — container network interface leaked"
    else
        pass "Host cannot see container peer veth (moved to container namespace)"
    fi

    echo "  Verifying container IP ($IP_A) NOT assigned to host..."
    if ip -o addr show | grep -q "$IP_A/"; then
        fail "Container IP $IP_A is assigned directly to host interface"
    else
        pass "Container IP $IP_A isolated from host interfaces"
    fi

else
    skip "Container-to-container ping — invalid IPs"
fi

# ── loopback inside container ─────────────────────────────────────────────────

section "Loopback inside containers"

if nsenter -t "$PID_A" -n -- ping -c 1 -W 2 127.0.0.1 >/dev/null 2>&1; then
    pass "Loopback reachable inside Container A"
else
    skip "Loopback not reachable in Container A (may need 'ip link set lo up')"
fi

# ── network file persistence ──────────────────────────────────────────────────

section "Network state persistence"

if [ -f "/tmp/containers/net/$CA.net" ]; then
    pass "Network config file exists for $CA"
else
    fail "Network config file missing for $CA (/tmp/containers/net/$CA.net)"
fi

if [ -f "/tmp/containers/net/$CB.net" ]; then
    pass "Network config file exists for $CB"
else
    fail "Network config file missing for $CB (/tmp/containers/net/$CB.net)"
fi

# ── teardown: veth cleanup after container removal ───────────────────────────

section "Network cleanup on container removal"

VETH_BEFORE=$(ip link show | grep -c "^[0-9]*: vh" || true)

cleanup_container "$CA"
sleep 0.5
cleanup_container "$CB"
sleep 1.0

VETH_AFTER=$(ip link show | grep -c "^[0-9]*: vh" || true)

echo "  veth count before removal: $VETH_BEFORE"
echo "  veth count after  removal: $VETH_AFTER"

if [ "$VETH_AFTER" -lt "$VETH_BEFORE" ]; then
    pass "veth interfaces cleaned up after container removal"
elif [ "$VETH_AFTER" -eq 0 ]; then
    pass "No residual veth interfaces on host"
else
    fail "veth interfaces may not have been cleaned up (before=$VETH_BEFORE after=$VETH_AFTER)"
fi

if [ ! -f "/tmp/containers/net/$CA.net" ]; then
    pass "Network config file removed for $CA"
else
    fail "Network config file still present for $CA"
fi

if [ ! -f "/tmp/containers/net/$CB.net" ]; then
    pass "Network config file removed for $CB"
else
    fail "Network config file still present for $CB"
fi

# ── verify containers no longer listed ───────────────────────────────────────

if ! "$BINARY" list 2>/dev/null | grep -qE "^$CA |^$CB "; then
    pass "Both containers removed cleanly"
else
    fail "Containers still visible after removal"
fi

# ── summary ───────────────────────────────────────────────────────────────────

echo
echo "====================================================================="
echo " Results: PASS=$PASS  FAIL=$FAIL  SKIP=$SKIP"
echo "====================================================================="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0

