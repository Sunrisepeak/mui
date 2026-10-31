#!/usr/bin/env bash
# Whether this mcpp links the Python extension (src/entry/python) so that
# CPython can load it: a shared library must hold the kernel-abi package it
# reaches (openkal-linux/-macos/-windows under mcpplibs.os). mcpp up to
# 2026.10.26.1 leaves `kal_*` undefined there — Linux links it and CPython
# refuses to import it, macOS and Windows refuse the link
# (mcpp-community/mcpp#846). Until the mcpp CI installs has the fix, the
# workflow leaves the extension out of the workspace's build and its wheel
# step, and says so; with it, everything runs, without a change here.
#
#   tools/ci/python-extension-probe.sh <profile>     writes ok=true|false to $GITHUB_OUTPUT
set -uo pipefail
cd "$(dirname "$0")/../.."
profile=${1:-dev}
out=${GITHUB_OUTPUT:-/dev/stdout}

say() {
    echo "ok=$1" >> "$out"
    [ "$1" = true ] || echo "::warning::$2 — the Python extension is left out of this run (mcpp-community/mcpp#846)"
    echo "python extension: $1${2:+ ($2)}"
}

if ! mcpp build -p openmui.entry.python --profile "$profile" > python-probe.log 2>&1; then
    tail -5 python-probe.log
    say false "this mcpp does not link the extension"
    exit 0
fi
lib=$(python3 - <<'PY'
import json
last = json.load(open("target/.last-result.json"))["data"]
result = json.load(open(last["result"]))["data"]
print(next((a["path"] for a in result.get("artifacts", []) if a.get("name") == "_openmui"), ""))
PY
)
if [ -z "$lib" ]; then
    say false "the build made no _openmui"
elif command -v nm > /dev/null && nm -u "$lib" 2>/dev/null | grep -q 'kal_'; then
    say false "this mcpp leaves kal_* undefined in it"
else
    say true ""
fi
