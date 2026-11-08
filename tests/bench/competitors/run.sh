#!/usr/bin/env bash
# Every comparison of this directory, on this machine: our side built
# natively and for wasm32-emscripten (omui.bench.competitors), the
# competitors' installed at their pinned versions in target/bench-competitors.
#
#   tests/bench/competitors/run.sh                 each comparison three times (the README's tables)
#   tests/bench/competitors/run.sh --ledger [N]    the lead ledger (Part 2 plan §2.2, D-135): N rounds
#                                                  (15 by default), our side and theirs alternating in
#                                                  this one job; tools/eval/lead.py judges each metric of
#                                                  lead.metrics.omui.toml by the ratio a round and writes
#                                                  .agents/docs/evaluation/lead.ledger.omui.jsonl and lead.md
#   LEAD_DRY_RUN=1 ... --ledger                    the verdicts printed, nothing written
#
# The Web's sides run in the same node, so the same V8.
set -euo pipefail
cd "$(dirname "$0")/../../.."
mode=tables
rounds=15
if [ "${1:-}" = "--ledger" ]; then
    mode=ledger
    rounds="${2:-15}"
fi
work=target/bench-competitors
mkdir -p "$work"
cp tests/bench/competitors/js/* "$work/"
(cd "$work" && npm install --silent --no-audit --no-fund)
tools/dev/capped.sh mcpp build --profile release -p omui.bench.competitors >/dev/null
tools/dev/capped.sh mcpp build --target wasm32-emscripten --profile release -p omui.bench.competitors >/dev/null
bin() { ls -t target/x86_64-linux-gnu/*/bin/bench.competitors/"$1" target/*/*/bin/bench.competitors/"$1" 2>/dev/null | head -1; }
web=$(ls -t target/wasm32-emscripten/*/bin/bench.competitors/omui-bench-motion.js | head -1)
motion=$(bin omui-bench-motion)
vector=$(bin omui-bench-vector)
markdown=$(bin omui-bench-markdown)
markdown_web=$(ls -t target/wasm32-emscripten/*/bin/bench.competitors/omui-bench-markdown.js | head -1)
# The markdown corpus: what people wrote in this repository (docs, the site's content), repeated to 1 MiB.
corpus="$work/markdown.corpus.md"
python3 - "$corpus" <<'PY'
import pathlib, sys
text = "\n\n".join(p.read_text(encoding="utf-8") for root in ("docs", "site/content") for p in sorted(pathlib.Path(root).rglob("*.md")))
out = text
while len(out.encode()) < 1 << 20:
    out += "\n\n" + text
pathlib.Path(sys.argv[1]).write_text(out, encoding="utf-8")
PY
echo "node $(node --version), $(nproc) threads"

if [ "$mode" = tables ]; then
    for run in 1 2 3; do
        echo "motion, native: $("$motion")"
        echo "motion, wasm:   $(node "$web")"
        echo "motion, gsap:   $(cd "$work" && node gsap-1000.mjs)"
    done
    # The vector scenes: written by our side, drawn by all three, compared.
    "$vector" --out "$work/vector"
    (cd "$work" && node vector.mjs vector)
    echo "markdown, native: $("$markdown" "$corpus")"
    echo "markdown, wasm:   $(node "$markdown_web" "$corpus")"
    (cd "$work" && node markdown.mjs "$(basename "$corpus")")
    exit 0
fi

samples="$work/samples.jsonl"
rm -f "$samples"
sample() { python3 tools/eval/lead.py sample --out "$samples" "$@"; }
for round in $(seq 1 "$rounds"); do
    echo "round $round of $rounds"
    sample --series hanim-native --key drive-1000 --round "$round" -- "$motion" --json
    sample --series hanim-wasm --key drive-1000 --round "$round" -- node "$web" --json
    sample --series gsap --key drive-1000 --round "$round" -- node "$work/gsap-1000.mjs" --json
    # xrender first: it writes the scenes and its pictures the others are compared with.
    sample --series xrender --round "$round" -- "$vector" --out "$work/vector" --runs 5
    sample --series skia --round "$round" -- node "$work/vector.mjs" "$work/vector" --runs 5
    sample --series mlibs.markdown-wasm --key markdown --round "$round" -- node "$markdown_web" "$corpus" --runs 5 --json
    sample --series markdown-competitors --series-field name --key markdown --round "$round" -- node "$work/markdown.mjs" "$corpus" --runs 5
done
if [ -n "${LEAD_DRY_RUN:-}" ]; then
    python3 tools/eval/lead.py verdict "$samples" --dry-run
else
    python3 tools/eval/lead.py verdict "$samples"
    python3 tools/eval/lead.py summary --out .agents/docs/evaluation/lead.md
fi
