#!/usr/bin/env bash
# Where the site's WebAssembly bytes go (W8, the Web's delivery; Q-38): the
# site built for wasm32-emscripten as its release is (-O2), its functions'
# names kept (the wasm-size profile), and `omui-gen wasm-size` over it — its
# sections, its code by owner (the C++ module, a namespace, a C library) and
# its largest functions — to target/wasm-size.json and target/wasm-size.md.
#
#   tools/ci/wasm-size.sh          MCPP=<path to mcpp> (default: mcpp on PATH)
set -euo pipefail
cd "$(dirname "$0")/../.."
mcpp=${MCPP:-mcpp}
"$mcpp" build -p openmui.site --target wasm32-emscripten --profile wasm-size > /dev/null \
  || { echo "::error::openmui.site did not build for the Web with its names kept"; exit 1; }
wasm=$(find "$PWD/target/wasm32-emscripten" -path "*/bin/site/omui-site.wasm" -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2)
[ -n "$wasm" ] || { echo "::error::no omui-site.wasm"; exit 1; }
"$mcpp" run -p openmui.gen omui-gen -- wasm-size "$wasm" --json "$PWD/target/wasm-size.json" --markdown "$PWD/target/wasm-size.md" --top 40 > /dev/null
python3 - target/wasm-size.json <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
kib = lambda b: f"{b / 1024:.0f} KiB"
print(f"site wasm: {kib(r['shipped'])} shipped — code {kib(r['code'])}, data {kib(r['data'])}; {r['functions']} functions ({r['named']} named)")
for o in r["owners"][:12]:
    print(f"  {kib(o['bytes']):>9}  {100 * o['bytes'] / r['code']:4.1f}%  {o['name']}")
PY
