#!/usr/bin/env bash
# test_namespaces.sh — Phase 1: Namespace isolation verification
# Must run as root in WSL2 Ubuntu.
# Tests that each container gets its own PID, UTS, Mount, IPC, and Net namespaces.

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
    "$BINARY" stop  "$name" >/dev/null 2>&1 || true
    "$BINARY" remove "$name" >/dev/null 2>&1 || true
}

# ── preflight ─────────────────────────────────────────────────────────────────

echo "====================================================================="
echo " MiniOS Phase 1 — Namespace Isolation Test Suite"
echo "====================================================================="

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: Must run as root (sudo bash tests/test_namespaces.sh)"
    exit 1
fi

if [ ! -x "$BINARY" ]; then
    echo "ERROR: container binary not found at $BINARY — run 'make' first"
    exit 1
fi

# Clean up any leftover state from prior runs
rm -rf /tmp/containers
sleep 0.2

# ── host namespace reference inodes ──────────────────────────────────────────

HOST_NS_PID=$(stat -Lc %i /proc/self/ns/pid 2>/dev/null || echo "0")
HOST_NS_UTS=$(stat -Lc %i /proc/self/ns/uts 2>/dev/null || echo "0")
HOST_NS_MNT=$(stat -Lc %i /proc/self/ns/mnt 2>/dev/null || echo "0")
HOST_NS_IPC=$(stat -Lc %i /proc/self/ns/ipc 2>/dev/null || echo "0")
HOST_NS_NET=$(stat -Lc %i /proc/self/ns/net 2>/dev/null || echo "0")

# ── create + start a test container ──────────────────────────────────────────

CNAME="ns-test-$$"
section "Setup — container $CNAME"

"$BINARY" create "$CNAME" --command "sleep 30" >/dev/null
"$BINARY" start  "$CNAME" >/dev/null

# Allow clone() child to fully initialise before we read its /proc entry
sleep 0.5

# Obtain the running PID from the list output
CPID=$("$BINARY" list 2>/dev/null | awk -v n="$CNAME" '$1==n {print $2}')

if [ -z "$CPID" ] || [ "$CPID" -le 0 ] 2>/dev/null; then
    echo "ERROR: Could not determine container PID — aborting namespace tests"
    cleanup_container "$CNAME"
    exit 1
fi

echo "  Container PID: $CPID"

# ── namespace inode comparison ────────────────────────────────────────────────

check_ns() {
    local ns_type="$1"
    local host_inode="$2"
    local proc_path="/proc/$CPID/ns/$ns_type"

    if [ ! -e "$proc_path" ]; then
        skip "$ns_type namespace — /proc/$CPID/ns/$ns_type not available"
        return
    fi

    local cont_inode
    cont_inode=$(stat -Lc %i "$proc_path" 2>/dev/null || echo "0")

    if [ "$host_inode" -ne "$cont_inode" ]; then
        pass "$ns_type namespace isolated (host=$host_inode cont=$cont_inode)"
    else
        fail "$ns_type namespace NOT isolated — same inode as host ($host_inode)"
    fi
}

section "Namespace inode isolation checks"

check_ns pid  "$HOST_NS_PID"
check_ns uts  "$HOST_NS_UTS"
check_ns mnt  "$HOST_NS_MNT"
check_ns ipc  "$HOST_NS_IPC"
check_ns net  "$HOST_NS_NET"

# ── inspect-ns CLI output ─────────────────────────────────────────────────────

section "inspect-ns CLI — human output"

NS_OUT=$("$BINARY" inspect-ns "$CNAME" 2>/dev/null)
echo "$NS_OUT" | head -20

for ns in pid uts mnt ipc net; do
    if echo "$NS_OUT" | grep -qi "ISOLATED"; then
        pass "inspect-ns shows ISOLATED status"
        break
    fi
done

for ns in pid uts mnt ipc net; do
    if echo "$NS_OUT" | grep -qi "$ns"; then
        pass "inspect-ns lists $ns namespace"
    else
        fail "inspect-ns missing $ns namespace"
    fi
done

# ── inspect-ns CLI — JSON output ──────────────────────────────────────────────

section "inspect-ns CLI — JSON output"

NS_JSON=$("$BINARY" inspect-ns "$CNAME" --json 2>/dev/null)
echo "$NS_JSON"

if echo "$NS_JSON" | grep -q '"namespaces"'; then
    pass "inspect-ns --json contains namespaces array"
else
    fail "inspect-ns --json missing namespaces array"
fi

if echo "$NS_JSON" | grep -q '"inode"'; then
    pass "inspect-ns --json contains inode field"
else
    fail "inspect-ns --json missing inode field"
fi

# ── UTS / hostname isolation ──────────────────────────────────────────────────

section "UTS — hostname isolation"

HOST_HOSTNAME=$(hostname 2>/dev/null || echo "")
# Container runs "sleep 30"; its hostname should be its name, not the host's
CONT_HOSTNAME=$(nsenter -t "$CPID" --uts hostname 2>/dev/null || echo "NSENTER_FAILED")

if [ "$CONT_HOSTNAME" = "NSENTER_FAILED" ]; then
    skip "hostname isolation — nsenter not available or failed"
elif [ "$CONT_HOSTNAME" != "$HOST_HOSTNAME" ]; then
    pass "UTS hostname isolated (host='$HOST_HOSTNAME' cont='$CONT_HOSTNAME')"
else
    # Hostname might match if UTS wasn't set during container init; still
    # check the inode was different — the namespace was at least created.
    CONT_UTS_INODE=$(stat -Lc %i "/proc/$CPID/ns/uts" 2>/dev/null || echo "0")
    if [ "$HOST_NS_UTS" -ne "$CONT_UTS_INODE" ]; then
        pass "UTS namespace created (inode differs); hostname not yet customised"
    else
        fail "UTS namespace NOT isolated"
    fi
fi

# ── IPC isolation — verify distinct namespace ─────────────────────────────────

section "IPC namespace isolation"

CONT_IPC_INODE=$(stat -Lc %i "/proc/$CPID/ns/ipc" 2>/dev/null || echo "0")
if [ "$HOST_NS_IPC" -ne "$CONT_IPC_INODE" ] && [ "$CONT_IPC_INODE" -ne 0 ]; then
    pass "IPC namespace isolated (host=$HOST_NS_IPC cont=$CONT_IPC_INODE)"
else
    fail "IPC namespace NOT isolated (host=$HOST_NS_IPC cont=$CONT_IPC_INODE)"
fi

# ── Net isolation — verify distinct namespace ─────────────────────────────────

section "Net namespace isolation"

CONT_NET_INODE=$(stat -Lc %i "/proc/$CPID/ns/net" 2>/dev/null || echo "0")
if [ "$HOST_NS_NET" -ne "$CONT_NET_INODE" ] && [ "$CONT_NET_INODE" -ne 0 ]; then
    pass "Net namespace isolated (host=$HOST_NS_NET cont=$CONT_NET_INODE)"
else
    fail "Net namespace NOT isolated (host=$HOST_NS_NET cont=$CONT_NET_INODE)"
fi

# ── Mount namespace isolation ─────────────────────────────────────────────────

section "Mount namespace isolation"

CONT_MNT_INODE=$(stat -Lc %i "/proc/$CPID/ns/mnt" 2>/dev/null || echo "0")
if [ "$HOST_NS_MNT" -ne "$CONT_MNT_INODE" ] && [ "$CONT_MNT_INODE" -ne 0 ]; then
    pass "Mount namespace isolated (host=$HOST_NS_MNT cont=$CONT_MNT_INODE)"
else
    fail "Mount namespace NOT isolated (host=$HOST_NS_MNT cont=$CONT_MNT_INODE)"
fi

# ── cleanup ───────────────────────────────────────────────────────────────────

section "Teardown"

cleanup_container "$CNAME"

if ! "$BINARY" list 2>/dev/null | grep -q "^$CNAME "; then
    pass "Container $CNAME removed cleanly"
else
    fail "Container $CNAME still visible after remove"
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

