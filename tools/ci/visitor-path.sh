#!/usr/bin/env bash
# The visitor's path (D-95; W3's CI case, tools/ci/visitor-path.mjs): the
# built site (target/site, from tools/ci/site.sh) in a real browser, paired
# with an agent through `omui connect`, read and operated by `omui page`, the
# interaction sweep through the page, and the bridge's end seen by the page.
# The report: target/visitor-path.json.
#
#   tools/ci/visitor-path.sh          OMUI_CHROME=<chromium> (else as browser.sh finds one), MCPP=<mcpp>
set -euo pipefail
cd "$(dirname "$0")/../.."
mcpp=${MCPP:-mcpp}
chrome=${OMUI_CHROME:-}
[ -n "$chrome" ] || chrome=$(ls -d "$HOME"/.cache/ms-playwright/chromium_headless_shell-*/*/chrome-headless-shell 2>/dev/null | sort -V | tail -1 || true)
if [ -z "$chrome" ]; then
  for c in chrome-headless-shell google-chrome google-chrome-stable chromium chromium-browser; do
    if command -v "$c" > /dev/null; then chrome=$(command -v "$c"); break; fi
  done
fi
[ -n "$chrome" ] || { echo "::error::no headless Chromium for the visitor's path"; exit 1; }
node_bin=$(command -v node || ls -d "$HOME"/.xlings/data/xpkgs/xim-x-node/*/bin/node 2>/dev/null | sort -V | tail -1)
[ -n "$node_bin" ] || { echo "::error::no node for the visitor's path"; exit 1; }
flags=()
[ "$("$node_bin" -p 'process.versions.node.split(".")[0]')" -ge 22 ] || flags+=(--experimental-websocket)
[ -f target/site/index.html ] || { echo "::error::no target/site (tools/ci/site.sh first)"; exit 1; }
"$mcpp" build -p omui.cli > /dev/null
omui=$(find "$PWD/target" -path "*/bin/*" -name omui -type f -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2)
status=0
CHROME=$chrome OMUI=$omui "$node_bin" "${flags[@]}" tools/ci/visitor-path.mjs --serve target/site > target/visitor-path.json || status=$?
python3 - target/visitor-path.json <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
print("the visitor's path:")
for s in r["steps"]:
    print(f"  {'ok  ' if s['ok'] else 'FAIL'} {s['what']}" + (f" — {s['detail'][:300]}" if s.get('detail') and not s['ok'] else ""))
PY
[ "$status" = 0 ] || echo "::error::the visitor's path failed (target/visitor-path.json)"
exit "$status"
