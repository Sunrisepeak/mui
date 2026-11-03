#!/usr/bin/env bash
# The built site (target/site, from tools/ci/site.sh) in a real browser,
# served as Pages serves it:
# 1. its delivery against its budget (site/delivery.budget.omui.json): a cold
#    visit over the budget's connection — transfer, first frame, readiness,
#    frame tails (tools/ci/site-perf.mjs, target/site-perf.json);
# 2. what assistive technology reads: the browser's accessibility tree — the
#    landmarks, the headings' levels, every control named, the language,
#    zoom — and the keyboard's way in by the skip link
#    (tools/ci/site-a11y.mjs, target/site-a11y.json).
#
#   tools/ci/site-browser.sh          OMUI_CHROME=<chromium> (else as browser.sh finds one)
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
a11y=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-a11y.mjs --serve target/site > target/site-a11y.json || a11y=$?
python3 - target/site-a11y.json <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
print(f"site accessibility ({r['tree']['nodes']} nodes in the browser's tree):")
for c in r["checks"]:
    print(f"  {'ok  ' if c['ok'] else 'FAIL'} {c['what']}")
PY
[ "$a11y" = 0 ] || { echo "::error::the browser's accessibility tree or the keyboard's way in is wrong (target/site-a11y.json)"; status=1; }
structure=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-structure.mjs target/site > target/site-structure.report.omui.json || structure=$?
python3 - target/site-structure.report.omui.json <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
print("site structure and handoff:")
for c in r["checks"]:
    print(f"  {'ok  ' if c['ok'] else 'FAIL'} {c['what']}")
PY
[ "$structure" = 0 ] || { echo "::error::site routing or static handoff failed (target/site-structure.report.omui.json)"; status=1; }
fonts=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-fonts.mjs target/site > target/site-fonts.report.omui.json || fonts=$?
python3 - target/site-fonts.report.omui.json <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
print("site first-frame font coverage:")
for c in r["checks"]:
    print(f"  {'ok  ' if c['ok'] else 'FAIL'} {c['what']}")
PY
[ "$fonts" = 0 ] || { echo "::error::first-frame glyph coverage or font handoff failed (target/site-fonts.report.omui.json)"; status=1; }
exit "$status"
