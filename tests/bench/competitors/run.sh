#!/usr/bin/env bash
# Every comparison of this directory, on this machine: hanim's side built
# natively and for wasm32-emscripten (omui.bench.web), the competitors'
# installed at their pinned versions in target/bench-competitors, then each
# run three times — the Web's two in the same node, so the same V8.
#
#   tests/bench/competitors/run.sh
set -euo pipefail
cd "$(dirname "$0")/../../.."
work=target/bench-competitors
mkdir -p "$work"
cp tests/bench/competitors/package.json tests/bench/competitors/*.mjs "$work/"
(cd "$work" && npm install --silent --no-audit --no-fund)
tools/dev/capped.sh mcpp build --profile release -p omui.bench.web >/dev/null
tools/dev/capped.sh mcpp build --target wasm32-emscripten --profile release -p omui.bench.web >/dev/null
native=$(ls -t target/x86_64-linux-gnu/*/bin/bench.web/omui-bench-web target/*/*/bin/bench.web/omui-bench-web 2>/dev/null | head -1)
web=$(ls -t target/wasm32-emscripten/*/bin/bench.web/omui-bench-web.js | head -1)
echo "node $(node --version), $(nproc) threads"
for run in 1 2 3; do
    echo "native: $("$native")"
    echo "wasm:   $(node "$web")"
    echo "gsap:   $(cd "$work" && node gsap-1000.mjs)"
done
