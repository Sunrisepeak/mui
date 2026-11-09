#!/usr/bin/env bash
# The products' commands as an Agent uses them (convergence plan §10.3): each
# operation of a catalog through its real command line, its JSON answer and
# exit code checked — hanim's animation tool, xrender's command (the GPU
# ones on the row's device: lavapipe or Metal, as tools/ci/gpu.sh sets) and
# xengine's (timelines edited and taken back to the very bytes).
set -euo pipefail
cd "$(dirname "$0")/../.."
mcpp build -p hanim.studio -p xrender.tool -p xengine.tool -p omui.gen ${PROFILE:+--profile "$PROFILE"} > /dev/null
newest() { find target -path "*/bin/*" -name "$1" -type f -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2; }
hanim=$(newest hanim)
xr=$(newest xrender)
xe=$(newest xengine)
gen=$(newest omui-gen)
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
expect() {  # expect <exit code> <command…>
    local want=$1; shift
    set +e; out=$("$@"); got=$?; set -e
    if [ "$got" != "$want" ]; then echo "FAIL ($got, wanted $want): $*"; echo "$out" | head -c 2000; exit 1; fi
    echo "ok   $*"
    last=$out
}

doc=modules/projects/hanim/studio/examples/bounce.animation.omui.json
expect 0 "$hanim" check "$doc"
grep -q '"markers":\[{"name":"landed"' <<< "$last"
expect 0 "$hanim" sample "$doc" --at 0 --at 1.0
grep -q '"id":"ball"' <<< "$last"
expect 0 "$hanim" render "$doc" --out "$dir/bounce" --frames 4
[ -s "$dir/bounce-003.png" ] && [ -s "$dir/bounce.strip.png" ] && [ -s "$dir/bounce.report.omui.json" ]
echo '{"version": 1, "objects": [{"id": "a", "shape": {"circle": 5}, "fill": "#fff"}], "program": {"do": "twirl", "object": "a"}}' > "$dir/bad.animation.omui.json"
expect 1 "$hanim" check "$dir/bad.animation.omui.json"
grep -q '"where":"/program/do"' <<< "$last"
expect 2 "$hanim" render "$doc"

fx=modules/projects/xrender/effect/tests/data
expect 0 "$xr" device
expect 0 "$xr" effect check "$fx/scanlines.effect.omui.json"
grep -q '"name":"strength"' <<< "$last"
expect 1 "$xr" effect check "$fx/greedy.effect.omui.json"
grep -q 'binds resources of its own' <<< "$last"
expect 0 "$xr" effect preview "$fx/scanlines.effect.omui.json" --out "$dir/scan.png" --param strength=0.7
[ -s "$dir/scan.png" ]
expect 0 "$xr" cts --report "$dir/render.cts.omui.json"
grep -q '"failed":\[\]' <<< "$last"
expect 2 "$xr" frobnicate

t="$dir/short.timeline.omui.json"
expect 0 "$gen" snapshot showcase/promo-video/short.timeline.omui.ts --out "$t"   # the JSON the tool works on
before=$(sha256sum "$t" | cut -d' ' -f1)
expect 0 "$xe" timeline check "$t"
grep -q '"length":"900f"' <<< "$last"
expect 0 "$xe" timeline at "$t" --time 1s
grep -q '"clip":"hook"' <<< "$last"
expect 0 "$xe" timeline edit "$t" split --track 0 --clip 0 --at 15f --dry-run
[ "$(sha256sum "$t" | cut -d' ' -f1)" = "$before" ]
expect 0 "$xe" timeline edit "$t" split --track 0 --clip 0 --at 15f
[ "$(sha256sum "$t" | cut -d' ' -f1)" != "$before" ]
expect 0 "$xe" timeline check "$t"
expect 1 "$xe" timeline edit "$t" split --track 0 --clip 0 --at 99999s
expect 0 "$xe" timeline undo "$t"
[ "$(sha256sum "$t" | cut -d' ' -f1)" = "$before" ]
expect 1 "$xe" timeline undo "$t"
echo "tools e2e: ok"
