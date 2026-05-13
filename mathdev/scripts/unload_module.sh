#!/usr/bin/env bash
# scripts/unload_module.sh
set -euo pipefail
if lsmod | grep -q '^mathdev '; then
    echo "[unload_module] Removing mathdev..."
    rmmod mathdev
    echo "[unload_module] Done."
else
    echo "[unload_module] mathdev is not loaded."
fi
