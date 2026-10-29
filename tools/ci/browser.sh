#!/usr/bin/env bash
# The Web row's half that only a real page can check (D-84 §6.3), in
# headless Chromium (tools/ci/browser.mjs serves the program on localhost
# and reads back what it prints):
#
#   1. openmui.rhi.webgpu's tests — the omui.rhi conformance suite among
#      them — on the page's WebGPU, built with the wasm-browser profile
#      (linked to suspend, so the suite can read back);
#   2. every WGSL entry point the Web build wrote (tools/ci/wgsl.mjs: the
#      shader partitions of the last wasm32-emscripten builds), compiled by
#      the browser.
#
# Chromium: OMUI_CHROME, else Playwright's headless shell under
# ~/.cache/ms-playwright, else chrome-headless-shell, google-chrome or
# chromium on PATH. Without one the checks are said to be skipped, and that
# is a failure only with OMUI_REQUIRE_BROWSER=1.
#
#   tools/ci/browser.sh          MCPP=<path to mcpp> (default: mcpp on PATH)
set -euo pipefail
cd "$(dirname "$0")/../.."
mcpp=${MCPP:-mcpp}

chrome=${OMUI_CHROME:-}
if [ -z "$chrome" ]; then
  chrome=$(ls -d "$HOME"/.cache/ms-playwright/chromium_headless_shell-*/*/chrome-headless-shell 2>/dev/null | sort -V | tail -1 || true)
fi
if [ -z "$chrome" ]; then
  for c in chrome-headless-shell google-chrome google-chrome-stable chromium chromium-browser; do
    if command -v "$c" > /dev/null; then chrome=$(command -v "$c"); break; fi
  done
fi
if [ -z "$chrome" ]; then
  if [ "${OMUI_REQUIRE_BROWSER:-}" = 1 ]; then echo "::error::no headless Chromium for the browser's checks"; exit 1; fi
  echo "::notice::no headless Chromium (OMUI_CHROME): the browser's checks (rhi.webgpu's conformance, WGSL) were not run"
  exit 0
fi
node_bin=$(command -v node || ls -d "$HOME"/.xlings/data/xpkgs/xim-x-node/*/bin/node 2>/dev/null | sort -V | tail -1)
[ -n "$node_bin" ] || { echo "::error::no node for the browser's checks"; exit 1; }
echo "browser: $("$chrome" --version 2>/dev/null || echo "$chrome")"

# 1. rhi.webgpu on the page's WebGPU.
"$mcpp" test --target wasm32-emscripten --profile wasm-browser -p openmui.rhi.webgpu --no-run \
  || { echo "::error::rhi.webgpu's tests did not build for the browser"; exit 1; }
program=$(ls -t target/wasm32-emscripten/*/bin/webgpu/webgpu_test.js 2>/dev/null | head -1)
[ -n "$program" ] || { echo "::error::webgpu_test.js was not built"; exit 1; }
"$node_bin" tools/ci/browser.mjs --chrome "$chrome" "$program" --require-webgpu \
  || { echo "::error::rhi.webgpu failed in the browser"; exit 1; }

# 2. Every WGSL entry point the Web build wrote: of each partition, the
# newest that holds WGSL.
mapfile -t partitions < <(python3 - <<'PY'
import glob, os
newest = {}
for f in glob.glob("target/.build-mcpp/out/*/*/openmui/shader/*_shaders.cppm"):
    key = (f.split(os.sep)[-4], os.path.basename(f))
    if "_wgsl{" not in open(f, encoding="utf-8").read():
        continue
    if key not in newest or os.path.getmtime(f) > os.path.getmtime(newest[key]):
        newest[key] = f
print("\n".join(sorted(newest.values())))
PY
)
[ ${#partitions[@]} -gt 0 ] || { echo "::error::no WGSL in the build's shader partitions (run tools/ci/web.sh first)"; exit 1; }
check="${TMPDIR:-/tmp}/omui-wgsl-check.js"
"$node_bin" tools/ci/wgsl.mjs "$check" "${partitions[@]}"
"$node_bin" tools/ci/browser.mjs --chrome "$chrome" "$check" || { echo "::error::the browser refused WGSL the build wrote"; exit 1; }
rm -f "$check"
