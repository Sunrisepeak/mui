#!/usr/bin/env bash
# The site's delivery measured in a real browser against its budget
# (site/delivery.budget.omui.json): the built page (target/site, from
# tools/ci/site.sh) served as Pages serves it, a cold visit over the budget's
# connection, its transfer, first frame, readiness and frame tails. The
# report is written to target/site-perf.json.
#
#   tools/ci/site-perf.sh             OMUI_CHROME=<chromium> (else as browser.sh finds one)
set -euo pipefail
cd "$(dirname "$0")/../.."
chrome=${OMUI_CHROME:-}
[ -n "$chrome" ] || chrome=$(ls -d "$HOME"/.cache/ms-playwright/chromium_headless_shell-*/*/chrome-headless-shell 2>/dev/null | sort -V | tail -1 || true)
if [ -z "$chrome" ]; then
  for c in chrome-headless-shell google-chrome google-chrome-stable chromium chromium-browser; do
    if command -v "$c" > /dev/null; then chrome=$(command -v "$c"); break; fi
  done
fi
[ -n "$chrome" ] || { echo "::error::no headless Chromium for the site's delivery"; exit 1; }
node_bin=$(command -v node || ls -d "$HOME"/.xlings/data/xpkgs/xim-x-node/*/bin/node 2>/dev/null | sort -V | tail -1)
[ -n "$node_bin" ] || { echo "::error::no node for the site's delivery"; exit 1; }
flags=()
# WebSocket is node's own from 22; 20 has it behind a flag.
[ "$("$node_bin" -p 'process.versions.node.split(".")[0]')" -ge 22 ] || flags+=(--experimental-websocket)
budget=site/delivery.budget.omui.json
network=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["network"])' "$budget")
status=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-perf.mjs --serve target/site --network "$network" --budget "$budget" > target/site-perf.json || status=$?
python3 - target/site-perf.json <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
print(f"site delivery: {r['transfer']['total'] / 1e6:.2f} MB, first frame {r['first_frame_ms']:.0f} ms, ready {r['ready_ms']:.0f} ms;"
      f" frames p50 {r['frames']['p50']:.1f} p95 {r['frames']['p95']:.1f} max {r['frames']['max']:.1f} ms, {r['frames']['long']} long")
for b in r.get("budgets", []):
    print(f"  {'ok  ' if b['pass'] else 'OVER'} {b['metric']}: {b['value']} (budget {b['max']})")
PY
[ "$status" = 0 ] || echo "::error::the site's delivery is over its budget (target/site-perf.json)"
exit "$status"
