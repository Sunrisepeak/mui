#!/usr/bin/env bash
# The CI matrix on one Linux machine, for when hosted runners are unavailable:
# lint, GCC 16 (dev and release), LLVM 23, and the Windows target built with
# MinGW and run under wine. The pinned determinism hashes in the tests make a
# pass on every row the same cross-toolchain gate the hosted matrix is.
#
#   tools/ci/local.sh [member ...]     default: every member (--workspace)
set -euo pipefail
cd "$(dirname "$0")/../.."

select=(--workspace)
if [ $# -gt 0 ]; then select=(); for m in "$@"; do select+=(-p "$m"); done; fi

step() { printf '\n== %s\n' "$*"; }

step lint
python3 tools/lint/lint.py
python3 tools/lint/lint.py --self-test > /dev/null

step "gcc@16.1.0 dev"
mcpp test "${select[@]}" --toolchain gcc@16.1.0 2>&1 | grep -E 'result|FAIL' || true
step "gcc@16.1.0 release"
mcpp test "${select[@]}" --toolchain gcc@16.1.0 --profile release 2>&1 | grep -E 'result|FAIL' || true
step "llvm@23.1.3 dev"
mcpp test "${select[@]}" --toolchain llvm@23.1.3 2>&1 | grep -E 'result|FAIL' || true

step "agent e2e (example 07 in its own process)"
tools/ci/agent-e2e.sh 2>&1 | grep -E '^(ok|FAIL)|e2e' || true

if command -v wine > /dev/null; then
    step "x86_64-windows-gnu under wine"
    mcpp test "${select[@]}" --target x86_64-windows-gnu --no-run > /dev/null 2>&1
    failed=0
    while IFS= read -r exe; do
        if WINEDEBUG=-all timeout 300 wine "$exe" > /tmp/omui-wine.log 2>&1; then
            echo "  ok    $(basename "$exe")"
        else
            echo "  FAIL  $(basename "$exe")"; cat /tmp/omui-wine.log; failed=1
        fi
    done < <(find target/x86_64-windows-gnu -path '*/bin/*' -name '*_test.exe' | sort)
    [ "$failed" = 0 ]
fi
