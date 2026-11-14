#!/usr/bin/env bash
# The site's program cut in two (D5; Part 2 plan §4.1, LD2): the functions
# its first screen runs first, the rest after the first frame.
#
#   1. wasm-split instruments a copy of the packed program;
#   2. a copy of the packed page runs it in headless Chromium at its first
#      screen, used as a visitor uses one (tools/ci/site-profile.mjs), and
#      the program writes which of its functions ran;
#   3. wasm-split cuts the packed program by that profile: the first half,
#      the second (the deferred);
#   4. omui-gen dist-toy-split puts both in the package — the first half in
#      place of the program, the second beside it under its content hash —
#      and says in the page where the second is: there the page compiles it
#      after the first frame (home), elsewhere before main.
#
# The bare page and #/zh (the home in each language) are the routes it is
# lazy on; the language part of a route is not its place (the loader strips
# it). The profile is this build's (wasm-split checks it belongs to the module),
# so it is made each time. Without a browser or wasm-split the page stays
# whole, with a notice — except under OMUI_REQUIRE_BROWSER=1 (CI).
#
#   tools/ci/site-split.sh [site dir]      default target/site; MCPP=<mcpp>
set -euo pipefail
cd "$(dirname "$0")/../.."
site=${1:-target/site}
mcpp=${MCPP:-mcpp}
work=target/site-split

fail() {
  if [ "${OMUI_REQUIRE_BROWSER:-}" = 1 ]; then echo "::error::site-split: $1"; exit 1; fi
  echo "::notice::site-split: $1; the program stays whole"; exit 0
}
split=$(ls -d "$HOME"/.mcpp/registry/data/xpkgs/xim-x-emsdk/*/bin/wasm-split "$HOME"/.xlings/data/xpkgs/xim-x-emsdk/*/bin/wasm-split 2>/dev/null | sort -V | tail -1 || true)
[ -n "$split" ] || fail "no wasm-split (Binaryen, from the emsdk mcpp installs)"
chrome=${OMUI_CHROME:-${CHROME:-}}
[ -n "$chrome" ] || chrome=$(ls -d "$HOME"/.cache/ms-playwright/chromium_headless_shell-*/*/chrome-headless-shell 2>/dev/null | sort -V | tail -1 || true)
if [ -z "$chrome" ]; then
  for c in chrome-headless-shell google-chrome google-chrome-stable chromium chromium-browser; do
    if command -v "$c" > /dev/null; then chrome=$(command -v "$c"); break; fi
  done
fi
[ -n "$chrome" ] || fail "no headless Chromium to profile the first screen in"

# What Emscripten's wasm32 output uses: wasm-split must read and write the
# same features (and none newer, as compact imports, that browsers reject).
features=(--enable-bulk-memory --enable-bulk-memory-opt --enable-nontrapping-float-to-int --enable-sign-ext
          --enable-mutable-globals --enable-multivalue --enable-reference-types --enable-call-indirect-overlong)

rm -rf "$work"
mkdir -p "$work"
wasm=$(python3 -c 'import re,sys; print(re.search(r"var omuiWasm = \"([^\"]+)\";", open(sys.argv[1], encoding="utf-8").read()).group(1))' "$site/index.html")
grep -q 'var omuiDeferred = null;' "$site/index.html" || { echo "::error::site-split: $site/index.html is split already, or its loader predates the split"; exit 1; }
cp "$site/$wasm" "$work/whole.wasm"
"$split" "${features[@]}" --instrument "$work/whole.wasm" -o "$work/instrumented.wasm" > "$work/instrument.log" 2>&1 \
  || { tail -5 "$work/instrument.log"; echo "::error::site-split: wasm-split could not instrument the program"; exit 1; }

cp -r "$site" "$work/page"
cp "$work/instrumented.wasm" "$work/page/$wasm"
python3 - "$work/page/index.html" "$wasm" "$(stat -c %s "$work/instrumented.wasm")" <<'PY'
import re, sys
page, wasm, size = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(page, encoding="utf-8").read()
text, count = re.subn(r'"%s":\d+' % re.escape(wasm), '"%s":%s' % (wasm, size), text)
if count != 1:
    sys.exit(f"site-split: the page states no decoded size for {wasm}")
open(page, "w", encoding="utf-8").write(text)
PY
# The first screen in each language the page speaks (the Chinese home draws
# with its own face, through code the English one never runs), merged.
CHROME="$chrome" node tools/ci/site-profile.mjs "$work/page" "$work/en.profile"
CHROME="$chrome" node tools/ci/site-profile.mjs "$work/page" "$work/zh.profile" --route /zh
"$split" "${features[@]}" --merge-profiles "$work/en.profile" "$work/zh.profile" -o "$work/first-screen.profile" > "$work/merge.log" 2>&1 \
  || { tail -5 "$work/merge.log"; echo "::error::site-split: wasm-split could not merge the profiles"; exit 1; }

"$split" "${features[@]}" --split --profile="$work/first-screen.profile" --export-prefix=% --placeholder-namespace=placeholder \
  "$work/whole.wasm" -o1 "$work/first.wasm" -o2 "$work/deferred.wasm" > "$work/split.log" 2>&1 \
  || { tail -5 "$work/split.log"; echo "::error::site-split: wasm-split could not split the program"; exit 1; }
"$mcpp" run omui-gen -p omui.gen -- dist-toy-split "$site" --primary "$work/first.wasm" --deferred "$work/deferred.wasm" --lazy ""
python3 - "$work" <<'PY'
import gzip, os, sys
work = sys.argv[1]
for name in ("whole.wasm", "first.wasm", "deferred.wasm"):
    data = open(os.path.join(work, name), "rb").read()
    print(f"site-split: {name:<14} {len(data) / 1048576:6.2f} MiB, gzip {len(gzip.compress(data, 9)) / 1048576:5.2f} MiB")
PY
rm -rf "$work/page"
