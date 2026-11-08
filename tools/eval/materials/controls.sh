#!/usr/bin/env bash
# The controls' side-by-side material (compare set `components`, D-135):
# ours — site's quality lab, each control's four states in light
# (site/tests/quality_lab_test.cpp) — and Material 3's, the same controls in
# the same states, cells and background, as Google's @material/web draws them
# (tests/bench/competitors/compare/material3). Both land in
# target/compare-material/{omui,material3}/<control>.png.
#
#   tools/eval/materials/controls.sh        CHROME=<chromium> (default: as tools/ci/browser.sh finds it)
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=target/compare-material
mkdir -p "$out/omui" "$out/material3/states"
chrome=${CHROME:-$(ls -d "$HOME"/.cache/ms-playwright/chromium_headless_shell-*/*/chrome-headless-shell 2>/dev/null | sort -V | tail -1)}

tools/dev/capped.sh mcpp test -p omui.site quality_lab_test > "$out/quality-lab.log" 2>&1 || { tail -20 "$out/quality-lab.log"; exit 1; }
lab=$(python3 -c 'import os,tempfile; print(os.path.join(tempfile.gettempdir(), "openmui-tests", "quality-lab", "controls"))')
cp "$lab"/*.png "$out/omui/"

side=tests/bench/competitors/compare/material3
(cd "$side" && npm install --silent --no-audit --no-fund && esbuild controls.js --bundle --loader:.woff2=dataurl --loader:.woff=dataurl --outfile=controls.bundle.js --log-level=warning)
CHROME="$chrome" node "$side/capture.mjs" "$out/material3/states"
python3 - "$out" <<'PY'
import os, sys
sys.path.insert(0, os.path.join(os.getcwd(), "tools", "eval"))
import png
out = sys.argv[1]
states = ("rest", "hover", "pressed", "focus")
names = sorted({f.rsplit("-", 1)[0] for f in os.listdir(os.path.join(out, "material3", "states")) if f.endswith(".png")})
for name in names:
    cells = [png.read(os.path.join(out, "material3", "states", f"{name}-{s}.png")) for s in states]
    png.write(png.row(cells), os.path.join(out, "material3", f"{name}.png"))
print(f"controls: {len(names)} Material 3 strips, {len(os.listdir(os.path.join(out, 'omui')))} of ours, in {out}")
PY
