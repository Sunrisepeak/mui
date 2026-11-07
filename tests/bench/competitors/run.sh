#!/usr/bin/env bash
# Every comparison of this directory, on this machine: our side built
# natively and for wasm32-emscripten (omui.bench.competitors), the
# competitors' installed at their pinned versions in target/bench-competitors,
# then each run three times — the Web's two in the same node, so the same V8.
#
#   tests/bench/competitors/run.sh
set -euo pipefail
cd "$(dirname "$0")/../../.."
work=target/bench-competitors
mkdir -p "$work"
cp tests/bench/competitors/js/* "$work/"
(cd "$work" && npm install --silent --no-audit --no-fund)
tools/dev/capped.sh mcpp build --profile release -p omui.bench.competitors >/dev/null
tools/dev/capped.sh mcpp build --target wasm32-emscripten --profile release -p omui.bench.competitors >/dev/null
bin() { ls -t target/x86_64-linux-gnu/*/bin/bench.competitors/"$1" target/*/*/bin/bench.competitors/"$1" 2>/dev/null | head -1; }
web=$(ls -t target/wasm32-emscripten/*/bin/bench.competitors/omui-bench-motion.js | head -1)
echo "node $(node --version), $(nproc) threads"
for run in 1 2 3; do
    echo "motion, native: $("$(bin omui-bench-motion)")"
    echo "motion, wasm:   $(node "$web")"
    echo "motion, gsap:   $(cd "$work" && node gsap-1000.mjs)"
done
# The vector scenes: written by our side, drawn by all three, compared.
"$(bin omui-bench-vector)" --out "$work/vector"
(cd "$work" && node vector.mjs vector)
