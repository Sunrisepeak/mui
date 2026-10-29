#!/usr/bin/env bash
# The Toy row (D-84 §6.5, §8.9): the games built for wasm32-emscripten and
# packaged for Bilibili Toy (`mcpp pack --format dist-toy` — `--pkg-format` in newer mcpp, openmui.rules'
# dist_toy()), each checked by Toy's own toy_doctor.py — pinned to
# github.com/bilibili/toy at ae156ec (MIT), downloaded into the build
# directory and held to its sha256. Every package's sizes are printed; Toy's
# hard limit, index.html at most 1 MiB, is the packager's error and the
# doctor's. Nothing is published (that is the maintainer's `toy create`).
#
#   tools/ci/toy.sh [package …]   default: the fighting game and the breakout
#   MCPP=<path to mcpp>           default: mcpp on PATH
#
# The packages are copied to target/toy/<target>/ (beside each, its
# <target>.toy.omui.json: the files and their sizes) — what a person serves
# to try one (python3 -m http.server -d target/toy/<target>).
set -euo pipefail
cd "$(dirname "$0")/../.."
mcpp=${MCPP:-mcpp}
doctor_rev=ae156ecb3391dfb22bdd93d45d6b0067e1e5b723
doctor_sha256=cbbf1244fff1179cfa48272cddb2cbcbd870da73d5fff1c5f30720231d6eeb0c
out=target/toy
mkdir -p "$out"

doctor="$out/toy_doctor-${doctor_rev:0:7}.py"
sha256() { python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }
if [ ! -f "$doctor" ] || [ "$(sha256 "$doctor")" != "$doctor_sha256" ]; then
    curl -fsSL --retry 3 "https://raw.githubusercontent.com/bilibili/toy/$doctor_rev/skills/toy/scripts/toy_doctor.py" -o "$doctor.part"
    [ "$(sha256 "$doctor.part")" = "$doctor_sha256" ] || { echo "::error::toy_doctor.py is not the pinned file"; exit 1; }
    mv "$doctor.part" "$doctor"
fi

packages=("$@")
[ ${#packages[@]} -gt 0 ] || packages=(openmui.showcase.fighting openmui.showcase.widget_breakout)
failed=0
for p in "${packages[@]}"; do
    echo "toy: $p"
    envelope="$out/$p.pack.json"
    if ! "$mcpp" pack -p "$p" --target wasm32-emscripten --release --format dist-toy --message-format json > "$envelope"; then
        echo "::error::$p did not pack for Toy"
        failed=1
        continue
    fi
    index=$(python3 -c 'import json,sys; print(next(a["path"] for a in json.load(open(sys.argv[1]))["data"]["artifacts"] if a["format"] == "dist-toy"))' "$envelope")
    built=$(dirname "$index")
    name=$(basename "$built")
    rm -rf "${out:?}/$name"
    cp -r "$built" "$out/$name"
    cp "$built.toy.omui.json" "$out/$name.toy.omui.json"
    python3 - "$out/$name.toy.omui.json" <<'PY'
import json, sys
about = json.load(open(sys.argv[1]))
mb = lambda n: f"{n / 1048576:.2f} MiB"
print(f"  index.html {about['index_bytes']} bytes (limit {about['index_limit']}); total {mb(about['total_bytes'])} in {len(about['files'])} files")
for f in sorted(about["files"], key=lambda f: -f["bytes"])[:4]:
    print(f"    {mb(f['bytes']):>10}  {f['path']}")
PY
    if ! python3 "$doctor" "$out/$name"; then
        echo "::error::Toy's doctor refused $name"
        failed=1
    fi
done
exit $failed
