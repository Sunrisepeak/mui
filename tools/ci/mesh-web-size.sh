#!/usr/bin/env bash
# What three dimensions weigh on a page (the foundation plan's M6.3: the Wasm size budget and the least display
# closure's budget): omui.size.yard — a yard drawn once by xrender.mesh.gpu on the page's WebGPU or WebGL2, no
# interface — built for wasm32-emscripten as a release is (-O2, the wasm-size profile: names kept, left out of
# the count), `omui-gen wasm-size` over it, and its bytes held to tests/size/yard/size.budget.omui.json.
#
#   tools/ci/mesh-web-size.sh          MCPP=<path to mcpp> (default: mcpp on PATH)
set -euo pipefail
cd "$(dirname "$0")/../.."
mcpp=${MCPP:-mcpp}
"$mcpp" build -p omui.size.yard --target wasm32-emscripten --profile wasm-size > /dev/null \
  || { echo "::error::omui.size.yard did not build for the Web"; exit 1; }
wasm=$(find "$PWD/target/wasm32-emscripten" -path "*/bin/yard/omui-size-yard.wasm" -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2)
[ -n "$wasm" ] || { echo "::error::no omui-size-yard.wasm"; exit 1; }
"$mcpp" run -p omui.gen omui-gen -- wasm-size "$wasm" --json "$PWD/target/mesh-web-size.json" --top 20 > /dev/null
python3 - target/mesh-web-size.json "${wasm%.wasm}.js" tests/size/yard/size.budget.omui.json <<'PY'
import json, os, sys
report = json.load(open(sys.argv[1]))
measured = {"wasm.shipped": report["shipped"], "wasm.code": report["code"], "wasm.data": report["data"], "js.bytes": os.path.getsize(sys.argv[2])}
print(f"a yard on a page: {measured['wasm.shipped']:,} B of Wasm shipped (code {measured['wasm.code']:,}, data {measured['wasm.data']:,}), {measured['js.bytes']:,} B of JavaScript")
for owner in report["owners"][:8]:
    print(f"  {owner['bytes']:>9,}  {100 * owner['bytes'] / report['code']:4.1f}%  {owner['name']}")
over = 0
for budget in json.load(open(sys.argv[3]))["budgets"]:
    value = measured[budget["metric"]]
    if value > budget["max"]:
        print(f"::error::{budget['metric']} is {value:,} B, over its budget of {budget['max']:,} B")
        over += 1
sys.exit(1 if over else 0)
PY
