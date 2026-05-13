#!/usr/bin/env bash
# scripts/run_tests.sh
# --------------------
# Run the freertos_master_slave test suite.
# Usage:
#   bash scripts/run_tests.sh          # build + unit tests + sim smoke test
#   bash scripts/run_tests.sh --unit   # unit tests only (no simulation)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${SCRIPT_DIR}/.."
cd "$ROOT"

MODE="${1:-all}"

# ── colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BOLD='\033[1m';   RESET='\033[0m'

echo -e "${BOLD}freertos_master_slave test suite${RESET}"
echo "================================="

# ── check cmake ───────────────────────────────────────────────────────────────
if ! command -v cmake &>/dev/null; then
    echo -e "${RED}cmake not found — install with: sudo apt install cmake${RESET}"
    exit 1
fi

# ── build ─────────────────────────────────────────────────────────────────────
echo -e "\n${BOLD}[build] Configuring and building${RESET}"
BUILD_LOG="/tmp/freertos_ms_build.log"
set +e
cmake -B build -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug > "$BUILD_LOG" 2>&1 &&
cmake --build build                                       >> "$BUILD_LOG" 2>&1
BUILD_RESULT=$?
set -e
if [[ $BUILD_RESULT -ne 0 ]]; then
    echo -e "  ${RED}Build failed${RESET} — see $BUILD_LOG"
    tail -20 "$BUILD_LOG" | sed 's/^/  /'
    exit 1
fi
echo -e "  ${GREEN}Build OK${RESET}"

# ── Unity unit tests ──────────────────────────────────────────────────────────
echo -e "\n${BOLD}[1/2] Unity unit tests${RESET}"
UNIT_RESULT=0
for bin in build/test_device_a_sm build/test_device_b_sm build/test_ipc_types; do
    name="$(basename "$bin")"
    echo -e "\n  ${BOLD}${name}${RESET}"
    tmpout="/tmp/${name}.out"
    set +e
    "$bin" > "$tmpout" 2>&1
    BIN_EXIT=$?
    set -e
    sed 's/^/    /' "$tmpout"
    [[ $BIN_EXIT -ne 0 ]] && UNIT_RESULT=$BIN_EXIT || true
done

if [[ "${MODE}" == "--unit" ]]; then
    echo ""
    [[ $UNIT_RESULT -eq 0 ]] && echo -e "${GREEN}Unit tests passed!${RESET}" \
                              || echo -e "${RED}Unit tests failed.${RESET}"
    exit $UNIT_RESULT
fi

# ── Simulation smoke test ─────────────────────────────────────────────────────
echo -e "\n${BOLD}[2/2] Simulation smoke test (5 s)${RESET}"
SIM_LOG="/tmp/freertos_ms_sim.log"
set +e
timeout 5 ./build/freertos_sim > "$SIM_LOG" 2>&1
SIM_EXIT=$?
set -e

# timeout returns 124 when it killed the process after the time limit;
# that means the sim started and ran without crashing, which is a pass.
if [[ $SIM_EXIT -eq 0 || $SIM_EXIT -eq 124 ]]; then
    SIM_RESULT=0
    echo -e "  ${GREEN}Simulation ran without crashing${RESET}"
    head -8 "$SIM_LOG" | sed 's/^/    /'
    echo "    ..."
    tail -3 "$SIM_LOG" | sed 's/^/    /'
else
    SIM_RESULT=$SIM_EXIT
    echo -e "  ${RED}Simulation crashed (exit $SIM_EXIT)${RESET}"
    sed 's/^/    /' "$SIM_LOG"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "================================="
echo -e "${BOLD}Summary:${RESET}"
[[ $UNIT_RESULT -eq 0 ]] && echo -e "  Unit tests      ${GREEN}PASS${RESET}" \
                         || echo -e "  Unit tests      ${RED}FAIL${RESET}"
[[ $SIM_RESULT  -eq 0 ]] && echo -e "  Sim smoke test  ${GREEN}PASS${RESET}" \
                         || echo -e "  Sim smoke test  ${RED}FAIL${RESET}"
echo ""

TOTAL=$((UNIT_RESULT + SIM_RESULT))
[[ $TOTAL -eq 0 ]] && echo -e "${GREEN}All tests passed!${RESET}" \
                   || echo -e "${RED}Some tests failed.${RESET}"
exit $TOTAL
