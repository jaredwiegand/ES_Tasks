#!/usr/bin/env bash
# scripts/load_module.sh
# ---------------------
# Load the mathdev kernel module and verify /dev/mathdev is present.
# Run as root or with sudo.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE="${SCRIPT_DIR}/../kernel/mathdev.ko"
DEVICE="/dev/mathdev"
DEVNODE_PERMS="0666"

# ── Check if already loaded ───────────────────────────────────────────────────
if lsmod | grep -q '^mathdev '; then
    echo "[load_module] mathdev already loaded."
    exit 0
fi

# ── Build if .ko is missing ───────────────────────────────────────────────────
if [[ ! -f "$MODULE" ]]; then
    echo "[load_module] mathdev.ko not found, building..."
    make -C "${SCRIPT_DIR}/../kernel"
fi

# ── Load ─────────────────────────────────────────────────────────────────────
echo "[load_module] Loading mathdev.ko..."
insmod "$MODULE"

# ── Wait for udev / device node ───────────────────────────────────────────────
for i in $(seq 1 10); do
    if [[ -c "$DEVICE" ]]; then
        echo "[load_module] Device node $DEVICE is ready."
        chmod $DEVNODE_PERMS "$DEVICE"
        echo "[load_module] Permissions set to $DEVNODE_PERMS."
        dmesg | tail -5
        exit 0
    fi
    sleep 0.2
done

echo "[load_module] WARNING: $DEVICE did not appear within 2 s."
echo "[load_module] Check dmesg for errors."
dmesg | tail -10
exit 1
