#!/usr/bin/env bash
# Runs a page's test program (built with mcpp's wasm-browser profile) in Chromium on this machine's real GPU:
# tools/ci/browser.sh runs the page on the software device CI has (SwiftShader); this is the browser on the
# hardware a developer has — the full Chromium Playwright installs, in a window on the display there is, its WebGPU
# through Vulkan on the NVIDIA driver omui-devtools gpu --real links. What the page's adapter is, the program prints
# (`caps().device`, and "software" where it is one).
#
# A display is needed: headless Chromium reaches Vulkan only through VK_EXT_headless_surface, which NVIDIA's driver
# does not have (ANGLE: "A requested extension is not supported"), and with no Vulkan the page is offered SwiftShader
# or nothing. Where there is none, this says so and stops — the measurement is then unverified (D-201).
#
#   tools/dev/browser-gpu.sh target/wasm32-emscripten/<hash>/bin/omui.showcase.quay/web_frame_test.js --require-webgpu
set -euo pipefail
cd "$(dirname "$0")/../.."
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
  echo "browser-gpu.sh: no display (DISPLAY, WAYLAND_DISPLAY): headless Chromium has no Vulkan on NVIDIA's driver (it asks VK_EXT_headless_surface); run this where a desktop session is" >&2
  exit 2
fi
chrome=${OMUI_CHROME:-$(ls -d "$HOME"/.cache/ms-playwright/chromium-*/chrome-linux64/chrome 2>/dev/null | sort -V | tail -1 || true)}
[ -n "$chrome" ] || { echo "browser-gpu.sh: no full Chromium (npx playwright install chromium, or OMUI_CHROME)" >&2; exit 1; }
node_bin=$(command -v node || ls -d "$HOME"/.xlings/data/xpkgs/xim-x-node/*/bin/node 2>/dev/null | sort -V | tail -1)
[ -n "$node_bin" ] || { echo "browser-gpu.sh: no node" >&2; exit 1; }
program=$1
shift
export OMUI_BROWSER_HEADED=1
exec mcpp run omui-devtools -p tools/devtools -- gpu --real -- "$node_bin" tools/ci/browser.mjs --chrome "$chrome" --timeout "${OMUI_BROWSER_TIMEOUT:-600}" \
  --flag --enable-features=Vulkan --flag --use-angle=vulkan --flag --ignore-gpu-blocklist \
  "$program" "$@"
