#!/usr/bin/env bash
# The website (D-84 §6.6, W3): openmui.site built for wasm32-emscripten and
# packed as a static page (dist-toy's package without Toy's SDK — a Pages
# subpath serves it as it is), then its places pre-rendered into that page by
# the same program built natively (`omui-site --prerender`): the documents as
# md4c-html writes them, in a section a reader without JavaScript and a
# search engine read, hidden when the program starts. The page is checked
# for what it must carry, and copied to target/site/ — what Pages uploads,
# and what a person serves to look at it (python3 -m http.server -d target/site).
#
#   tools/ci/site.sh              MCPP=<path to mcpp> (default: mcpp on PATH)
set -euo pipefail
cd "$(dirname "$0")/../.."
mcpp=${MCPP:-mcpp}
out=target/site
mkdir -p target

envelope=target/site.pack.json
"$mcpp" pack -p openmui.site --target wasm32-emscripten --release --format dist-toy --message-format json > "$envelope" \
  || { echo "::error::openmui.site did not pack for the Web"; exit 1; }
index=$(python3 -c 'import json,sys; print(next(a["path"] for a in json.load(open(sys.argv[1]))["data"]["artifacts"] if a["format"] == "dist-toy"))' "$envelope")
rm -rf "$out"
cp -r "$(dirname "$index")" "$out"

"$mcpp" run omui-site -p openmui.site -- --prerender "$out/index.html" \
  || { echo "::error::the site's places were not pre-rendered"; exit 1; }

python3 - "$out" <<'PY'
import os, sys
out = sys.argv[1]
page = open(os.path.join(out, "index.html"), encoding="utf-8").read()
need = ['<main id="omui-static"', '<section id="/home">', '<section id="/features">', "<h1>One tree, many renderings</h1>",
        "Module.onRuntimeInitialized = function", "@font-face", '<canvas id="canvas"']
missing = [n for n in need if n not in page]
if missing:
    sys.exit(f"::error::the page lacks {missing}")
if "toy-sdk" in page:
    sys.exit("::error::the site loads Toy's SDK")
files = [(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs]
total = sum(os.path.getsize(f) for f in files)
print(f"site: index.html {len(page.encode())} bytes; {len(files)} files, {total / 1048576:.2f} MiB")
for f in sorted(files, key=os.path.getsize, reverse=True)[:4]:
    print(f"  {os.path.getsize(f) / 1048576:>8.2f} MiB  {os.path.relpath(f, out)}")
PY
