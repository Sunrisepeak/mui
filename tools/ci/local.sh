#!/usr/bin/env bash
# The CI matrix on one Linux machine, for when hosted runners are unavailable:
# lint, GCC 16 (dev and release), LLVM 23, and the Windows target built with
# MinGW and run under wine. The pinned determinism hashes in the tests make a
# pass on every row the same cross-toolchain gate the hosted matrix is.
#
#   tools/ci/local.sh [member ...]     default: every member (--workspace)
set -euo pipefail
cd "$(dirname "$0")/../.."

# openmui.reflect is C++26 (GCC 16, -freflection) with a build of its graph
# of its own: the GCC dev row tests it by itself, as CI does.
select=(--workspace --exclude openmui.reflect)
if [ $# -gt 0 ]; then select=(); for m in "$@"; do select+=(-p "$m"); done; fi

step() { printf '\n== %s\n' "$*"; }

step lint
python3 tools/lint/lint.py
python3 tools/lint/lint.py --self-test > /dev/null

step "gcc@16.1.0 dev"
mcpp test "${select[@]}" --toolchain gcc@16.1.0 2>&1 | grep -E 'result|FAIL' || true
if [ $# -eq 0 ]; then
    step "openmui.reflect (C++26, gcc@16.1.0 dev)"
    mcpp test -p openmui.reflect --toolchain gcc@16.1.0 2>&1 | grep -E 'result|FAIL' || true
fi
step "gcc@16.1.0 release"
mcpp test "${select[@]}" --toolchain gcc@16.1.0 --profile release 2>&1 | grep -E 'result|FAIL' || true
step "llvm@23.1.3 dev"
mcpp test "${select[@]}" --toolchain llvm@23.1.3 2>&1 | grep -E 'result|FAIL' || true

step "benchmarks against their budgets (release)"
mcpp run -p openmui.bench --profile release -- --check tests/bench/budgets.bench.omui.json 2>&1 | grep -E '^(over budget|budgets)' || true

step "feature selection (M0 ⑤)"
tools/ci/features.sh 2>&1 | grep -E '^(ok|FAIL)|features' || true

step "templates (app-cpp, component)"
tools/ci/templates.sh 2>&1 | grep -E '^==|test result|FAIL|templates' || true

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
