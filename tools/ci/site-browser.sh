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
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-perf.mjs --serve target/site --network "$network" --budget "$budget" --request-log target/site-perf-requests.report.omui.json > target/site-perf.json || status=$?
python3 - target/site-perf.json <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
print(f"site delivery: {r['transfer']['total'] / 1e6:.2f} MB, first frame {r['first_frame_ms']:.0f} ms, ready {r['ready_ms']:.0f} ms;"
      f" frames p50 {r['frames']['p50']:.1f} p95 {r['frames']['p95']:.1f} max {r['frames']['max']:.1f} ms, {r['frames']['long']} long")
for b in r.get("budgets", []):
    print(f"  {'ok  ' if b['pass'] else 'OVER'} {b['metric']}: {b['value']} (budget {b['max']})")
PY
[ "$status" = 0 ] || echo "::error::the site's delivery is over its budget (target/site-perf.json)"
no_cache=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-perf.mjs --serve target/site --cache disabled --network "$network" --budget "$budget" --request-log target/site-perf-no-cache-requests.report.omui.json > target/site-perf-no-cache.report.omui.json || no_cache=$?
python3 - target/site-perf-no-cache.report.omui.json <<'NO_CACHE'
import json, sys
r = json.load(open(sys.argv[1]))
print(f"site delivery without HTTP cache: {r['transfer']['total']} bytes, first frame {r['first_frame_ms']:.0f} ms")
for b in r.get("budgets", []):
    print(f"  {'ok  ' if b['pass'] else 'OVER'} {b['metric']}: {b['value']} (budget {b['max']})")
NO_CACHE
[ "$no_cache" = 0 ] || { echo "::error::delivery without HTTP cache is over the same budget"; status=1; }
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
catalog=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-catalog.mjs target/site > target/site-catalog.report.omui.json || catalog=$?
python3 - target/site-catalog.report.omui.json <<'CATALOG'
import json, sys
r = json.load(open(sys.argv[1]))
print("site component catalog:")
for c in r["checks"]:
    print(f"  {'ok  ' if c['ok'] else 'FAIL'} {c['what']}")
CATALOG
[ "$catalog" = 0 ] || { echo "::error::component catalog browser checks failed (target/site-catalog.report.omui.json)"; status=1; }
workspace=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-workspace.mjs target/site > target/site-workspace.report.omui.json || workspace=$?
python3 - target/site-workspace.report.omui.json <<'WORKSPACE'
import json, sys
r = json.load(open(sys.argv[1]))
print("site release workspace:")
for c in r["checks"]:
    print(f"  {'ok  ' if c['ok'] else 'FAIL'} {c['what']}")
WORKSPACE
[ "$workspace" = 0 ] || { echo "::error::release workspace checks failed (target/site-workspace.report.omui.json)"; status=1; }
reactions=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-reactions.mjs target/site > target/site-reactions.report.omui.json || reactions=$?
python3 - target/site-reactions.report.omui.json <<'REACTIONS'
import json, sys
r = json.load(open(sys.argv[1]))
print("site reaction workspace:")
for c in r["checks"]:
    print(f"  {'ok  ' if c['ok'] else 'FAIL'} {c['what']}")
REACTIONS
[ "$reactions" = 0 ] || { echo "::error::reaction workspace checks failed (target/site-reactions.report.omui.json)"; status=1; }
motion=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-motion.mjs target/site > target/site-motion.report.omui.json || motion=$?
python3 - target/site-motion.report.omui.json <<'MOTION'
import json, sys
r = json.load(open(sys.argv[1]))
print("site motion, creative demos and Agent native:")
for c in r["checks"]:
    print(f"  {'ok  ' if c['ok'] else 'FAIL'} {c['what']}")
MOTION
[ "$motion" = 0 ] || { echo "::error::motion and creative browser checks failed (target/site-motion.report.omui.json)"; status=1; }
presentation=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-presentation.mjs target/site > target/site-presentation.report.omui.json || presentation=$?
python3 - target/site-presentation.report.omui.json <<'PRESENTATION'
import json, sys
r = json.load(open(sys.argv[1]))
print("site first views, mobile comparison and Agent handoff:")
for c in r["checks"]:
    print(f"  {'ok  ' if c['ok'] else 'FAIL'} {c['what']}")
PRESENTATION
[ "$presentation" = 0 ] || { echo "::error::site progressive presentation checks failed"; status=1; }
loading=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-loading.mjs target/site > target/site-loading.report.omui.json || loading=$?
python3 - target/site-loading.report.omui.json <<'LOADING'
import json, sys
r = json.load(open(sys.argv[1]))
print("site opening assets and loading:")
for c in r["checks"]:
    print(f"  {'ok  ' if c['ok'] else 'FAIL'} {c['what']}")
LOADING
[ "$loading" = 0 ] || { echo "::error::opening pixels, progress or retry failed (target/site-loading.report.omui.json)"; status=1; }
sharing=0
CHROME=$chrome "$node_bin" "${flags[@]}" tools/ci/site-font-sharing.mjs target/site > target/site-font-sharing.report.omui.json || sharing=$?
python3 - target/site-font-sharing.report.omui.json <<'SHARING'
import json, sys
r = json.load(open(sys.argv[1]))
print("site shared font delivery:")
for c in r["checks"]:
    print(f"  {'ok  ' if c['ok'] else 'FAIL'} {c['what']}")
SHARING
[ "$sharing" = 0 ] || { echo "::error::static and runtime fonts fetched twice or failed shared ownership/retry (target/site-font-sharing.report.omui.json)"; status=1; }
exit "$status"
