#!/usr/bin/env bash
# scripts/start_server.sh
# -----------------------
# Start the mathdev Python server in the foreground.
# The kernel module must already be loaded.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEVICE="/dev/mathdev"
SOCKET="/tmp/mathdev.sock"
LOG_LEVEL="${LOG_LEVEL:-INFO}"

if [[ ! -c "$DEVICE" ]]; then
    echo "[start_server] ERROR: $DEVICE not found."
    echo "Run: sudo scripts/load_module.sh"
    exit 1
fi

echo "[start_server] Starting mathdev server..."
exec python3 "${SCRIPT_DIR}/../server/server.py" \
    --socket "$SOCKET" \
    --device "$DEVICE" \
    --log-level "$LOG_LEVEL"
