#!/usr/bin/env bash
# scripts/init_git.sh
# -------------------
# Initialise a git repository for the project.
# Run once from the project root.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

git init
git add .
git commit -m "feat: initial mathdev project

- Linux kernel character device (mathdev.ko)
  - ioctl interface: MATH_IOCTL_CALC + MATH_IOCTL_QUERY_OPS
  - Operators: ADD, SUB, MUL, DIV on signed 64-bit integers
  - Per-open-instance state with mutex protection

- Unix-domain socket server (server.py)
  - Multi-client via threads
  - Length-prefixed JSON protocol
  - Service announcement (HELLO_ACK lists all ops from kernel)
  - Error handling with typed error codes

- Python 3.10+ client (client.py)
  - Dynamic menu built from server announcement
  - Coloured terminal UI

- C11 client (client.c) [bonus]
  - Identical protocol and UX to Python client
  - Uses cJSON (auto-fetched via CMake FetchContent)

- CMake build system
  - Builds C client + kernel module (custom target)
  - auto-fetches cJSON if not installed

- Mock server (mock_server.py) for testing without kernel module
"

echo ""
echo "Git repository initialised."
echo "To push to GitHub:"
echo "  git remote add origin https://github.com/<user>/mathdev.git"
echo "  git push -u origin main"
