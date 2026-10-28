#!/usr/bin/env bash
# The Web row (D-84): every workspace member whose manifest says it runs on
# the Web (`[package] platforms` lists "emscripten") is built for
# wasm32-emscripten and its tests run under node — with the same pinned
# hashes as every other row, so a green Web row is the determinism gate for
# the browser too. The members' list is the manifests'; `tools/lint`'s
# `platforms` rule holds that each depends only on members that say it too.
#
#   tools/ci/web.sh [profile]     default: wasm-test (tests read their fixtures through node)
set -euo pipefail
cd "$(dirname "$0")/../.."
profile=${1:-wasm-test}

mapfile -t packages < <(python3 - <<'PY'
import tomllib
root = tomllib.load(open("mcpp.toml", "rb"))
for m in root["workspace"]["members"]:
    p = tomllib.load(open(f"{m}/mcpp.toml", "rb")).get("package", {})
    if "emscripten" in p.get("platforms", []):
        print(f"{p['namespace']}.{p['name']}" if p.get("namespace") else p["name"])
PY
)
[ ${#packages[@]} -gt 0 ] || { echo "::error::no member says it runs on the Web"; exit 1; }
echo "web: ${#packages[@]} members: ${packages[*]}"
select=()
for p in "${packages[@]}"; do select+=(-p "$p"); done
log="${TMPDIR:-/tmp}/openmui-web.log"
mcpp test --target wasm32-emscripten --profile "$profile" "${select[@]}" 2>&1 | tee "$log"
grep -q "workspace result ok\|test result ok" "$log" || { echo "::error::the Web row's tests failed"; exit 1; }
