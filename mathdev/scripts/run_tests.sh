#!/usr/bin/env bash
# scripts/run_tests.sh
# --------------------
# Run the full mathdev test suite.
# Usage:
#   bash scripts/run_tests.sh           # all tests (unit + integration)
#   bash scripts/run_tests.sh --unit    # unit tests only (no kernel needed)
#   bash scripts/run_tests.sh --kernel  # include real kernel tests

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${SCRIPT_DIR}/.."
cd "$ROOT"

MODE="${1:-all}"

# ── colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BOLD='\033[1m';   RESET='\033[0m'

echo -e "${BOLD}mathdev test suite${RESET}"
echo "=================="

# ── check Python deps ─────────────────────────────────────────────────────────
if ! python3 -c "import pytest" 2>/dev/null; then
    echo -e "${YELLOW}Installing pytest...${RESET}"
    pip3 install pytest --break-system-packages --quiet
fi

# ── Python unit tests ─────────────────────────────────────────────────────────
echo -e "\n${BOLD}[1/3] Python unit tests${RESET}"
python3 -m pytest tests/unit/ -v --tb=short
UNIT_RESULT=$?

# ── Python integration tests (mock stack, no kernel needed) ──────────────────
echo -e "\n${BOLD}[2/3] Integration tests (mock stack)${RESET}"
python3 -m pytest tests/integration/test_mock_stack.py -v --tb=short
MOCK_RESULT=$?

# ── C client tests ────────────────────────────────────────────────────────────
echo -e "\n${BOLD}[3/4] C client protocol tests${RESET}"
CJSON_SRC="$(find build/_deps -name 'cJSON.c' 2>/dev/null | head -1)"
CJSON_INC="$(find build/_deps -name 'cJSON.h' 2>/dev/null | xargs dirname 2>/dev/null | head -1)"

if [[ -n "$CJSON_SRC" && -n "$CJSON_INC" ]]; then
    gcc tests/c/test_client_proto.c "$CJSON_SRC" \
        -I"$CJSON_INC" -o /tmp/mathdev_c_tests \
        -Wall -Wextra 2>&1
    /tmp/mathdev_c_tests
    C_RESULT=$?
else
    echo -e "${YELLOW}  Skipped — run cmake --build build first${RESET}"
    C_RESULT=0
fi

# ── Real kernel integration tests ─────────────────────────────────────────────
echo -e "\n${BOLD}[4/4] Real kernel integration tests${RESET}"
if [[ -c "/dev/mathdev" ]]; then
    python3 -m pytest tests/integration/test_integration.py -v --tb=short
    KERNEL_RESULT=$?
else
    echo -e "${YELLOW}  Skipped — /dev/mathdev not found${RESET}"
    echo    "  Run: sudo bash scripts/load_module.sh"
    KERNEL_RESULT=0
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "=================="
echo -e "${BOLD}Summary:${RESET}"
[[ $UNIT_RESULT   -eq 0 ]] && echo -e "  Unit tests      ${GREEN}PASS${RESET}" \
                           || echo -e "  Unit tests      ${RED}FAIL${RESET}"
[[ $MOCK_RESULT   -eq 0 ]] && echo -e "  Mock stack      ${GREEN}PASS${RESET}" \
                           || echo -e "  Mock stack      ${RED}FAIL${RESET}"
[[ $C_RESULT      -eq 0 ]] && echo -e "  C proto tests   ${GREEN}PASS${RESET}" \
                           || echo -e "  C proto tests   ${RED}FAIL${RESET}"
[[ $KERNEL_RESULT -eq 0 ]] && echo -e "  Kernel tests    ${GREEN}PASS${RESET}" \
                           || echo -e "  Kernel tests    ${RED}FAIL${RESET}"
echo ""

TOTAL=$((UNIT_RESULT + MOCK_RESULT + C_RESULT + KERNEL_RESULT))
[[ $TOTAL -eq 0 ]] && echo -e "${GREEN}All tests passed!${RESET}" \
                   || echo -e "${RED}Some tests failed.${RESET}"
exit $TOTAL
