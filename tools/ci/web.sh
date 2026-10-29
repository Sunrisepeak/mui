#!/usr/bin/env bash
# The Web row (D-84): every workspace member whose manifest says it runs on
# the Web (`[package] platforms` lists "emscripten") is built for
# wasm32-emscripten and its tests run under node — with the same pinned
# hashes as every other row, so a green Web row is the determinism gate for
# the browser too. The members' list is the manifests'; `tools/lint`'s
# `platforms` rule holds that each depends only on members that say it too.
# openmui.host.web is built and tested here too (its dependencies do not
# say they run on the Web yet, so it cannot), and then the page's half of it
# (src/js/omui_web.js: the hidden textarea, the semantic DOM mirror,
# window.omui, the clipboard, localStorage) under node's test runner with a
# fake DOM — alone, and with the program: tests/page_test.cpp in a fake page.
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
select+=(-p openmui.host.web)
log="${TMPDIR:-/tmp}/openmui-web.log"
mcpp test --target wasm32-emscripten --profile "$profile" "${select[@]}" 2>&1 | tee "$log"
grep -q "workspace result ok\|test result ok" "$log" || { echo "::error::the Web row's tests failed"; exit 1; }

# node: the runner's own, or the one xlings installed for emsdk.
node_bin=$(command -v node || ls -d "$HOME"/.xlings/data/xpkgs/xim-x-node/*/bin/node 2>/dev/null | sort -V | tail -1)
[ -n "$node_bin" ] || { echo "::error::no node for the page's tests"; exit 1; }
page_test=$(ls -t target/wasm32-emscripten/*/bin/web/page_test.js 2>/dev/null | head -1)
[ -n "$page_test" ] || { echo "::error::host.web's page_test was not built"; exit 1; }
OMUI_PAGE_TEST="$page_test" "$node_bin" --test modules/ui/host/web/tests/js/page.test.mjs modules/ui/host/web/tests/js/wasm.test.mjs \
  || { echo "::error::the page's half of host.web failed its tests"; exit 1; }
