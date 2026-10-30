#!/usr/bin/env bash
# The Web row's half that only a real page can check (D-84 §6.3), in
# headless Chromium (tools/ci/browser.mjs serves the program on localhost
# and reads back what it prints):
#
#   1. openmui.rhi.webgpu's and openmui.rhi.webgl's tests — the omui.rhi
#      conformance suite among them — on the page's WebGPU and WebGL2, built
#      with the wasm-browser profile (linked to suspend, so the suite can
#      read WebGPU back);
#   2. openmui.host.web drawing its frames on each: what it chooses, and its
#      frame of a page within the GPU renderer's tolerances of the CPU's;
#   3. every WGSL and ESSL entry point the Web build wrote
#      (tools/ci/shaders.mjs: the shader partitions of the last
#      wasm32-emscripten builds), compiled by the browser — WebGPU's
#      compiler, WebGL2's — and each file's vertex and fragment ESSL linked.
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
  echo "::notice::no headless Chromium (OMUI_CHROME): the browser's checks (WebGPU's and WebGL2's conformance, the shaders) were not run"
  exit 0
fi
node_bin=$(command -v node || ls -d "$HOME"/.xlings/data/xpkgs/xim-x-node/*/bin/node 2>/dev/null | sort -V | tail -1)
[ -n "$node_bin" ] || { echo "::error::no node for the browser's checks"; exit 1; }
echo "browser: $("$chrome" --version 2>/dev/null || echo "$chrome")"

# 1. rhi.webgpu and rhi.webgl on the page's WebGPU and WebGL2.
"$mcpp" test --target wasm32-emscripten --profile wasm-browser -p openmui.rhi.webgpu -p openmui.rhi.webgl --no-run \
  || { echo "::error::the page backends' tests did not build for the browser"; exit 1; }
for backend in webgpu webgl; do
  program=$(ls -t target/wasm32-emscripten/*/bin/$backend/${backend}_test.js 2>/dev/null | head -1)
  [ -n "$program" ] || { echo "::error::${backend}_test.js was not built"; exit 1; }
  "$node_bin" tools/ci/browser.mjs --chrome "$chrome" "$program" --require-$backend \
    || { echo "::error::rhi.$backend failed in the browser"; exit 1; }
done

# 2. host.web drawing on the page's WebGPU, then its WebGL2 (a canvas keeps
# the first kind of context it gives: one program run for each).
"$mcpp" test --target wasm32-emscripten --profile wasm-browser -p openmui.host.web --no-run \
  || { echo "::error::host.web's tests did not build for the browser"; exit 1; }
drawing=$(ls -t target/wasm32-emscripten/*/bin/web/drawing_test.js 2>/dev/null | head -1)
[ -n "$drawing" ] || { echo "::error::host.web's drawing_test.js was not built"; exit 1; }
# Headless Chromium's SwiftShader differs by build and machine: some lose a
# WebGPU device that presents to a canvas ("Device was destroyed") unless
# their Vulkan is SwiftShader's too (under which ANGLE's WebGL2 reads back
# wrong), others offer no WebGPU adapter with those switches. WebGPU's run
# takes the default switches, else the Vulkan ones; the test is the same.
# The fonts as dist-toy's page gives them: so the page draws its text.
fonts="--preload modules/content/text/fonts:/fonts"
"$node_bin" tools/ci/browser.mjs --chrome "$chrome" $fonts "$drawing" --webgpu \
  || "$node_bin" tools/ci/browser.mjs --chrome "$chrome" --flag --enable-features=Vulkan --flag --use-vulkan=swiftshader --flag --use-angle=vulkan \
       $fonts "$drawing" --webgpu \
  || { echo "::error::host.web did not draw on WebGPU in the browser"; exit 1; }
"$node_bin" tools/ci/browser.mjs --chrome "$chrome" $fonts "$drawing" --webgl \
  || { echo "::error::host.web did not draw on WebGL2 in the browser"; exit 1; }

# 3. Every shader the Web build wrote for the page: of each partition, the
# newest that holds WGSL or ESSL.
mapfile -t partitions < <(python3 - <<'PY'
import glob, os
newest = {}
for f in glob.glob("target/.build-mcpp/out/*/*/openmui/shader/*_shaders.cppm"):
    key = (f.split(os.sep)[-4], os.path.basename(f))
    text = open(f, encoding="utf-8").read()
    if "_wgsl{" not in text and "_essl{" not in text:
        continue
    if key not in newest or os.path.getmtime(f) > os.path.getmtime(newest[key]):
        newest[key] = f
print("\n".join(sorted(newest.values())))
PY
)
[ ${#partitions[@]} -gt 0 ] || { echo "::error::no WGSL or ESSL in the build's shader partitions (run tools/ci/web.sh first)"; exit 1; }
check="${TMPDIR:-/tmp}/omui-shaders-check.js"
"$node_bin" tools/ci/shaders.mjs "$check" "${partitions[@]}"
"$node_bin" tools/ci/browser.mjs --chrome "$chrome" "$check" || { echo "::error::the browser refused shaders the build wrote"; exit 1; }
rm -f "$check"
