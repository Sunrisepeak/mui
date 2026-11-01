#!/usr/bin/env bash
# The interaction sweep (strategy §4, a hard gate, D-91): every program, run
# headless as its own process, and `omui agent sweep` over it — every node an
# agent can operate, every action performed (what can be undone, undone; what
# a person must answer, only asked). A dead control fails it. Each program's
# verdicts are kept in target/sweep/<program>.sweep.omui.jsonl.
set -euo pipefail
cd "$(dirname "$0")/../.."

# package : binary : its directory (where it runs: a player reads its mcpp.toml there)
programs=(
    openmui.example.counter:tally:examples/03-counter
    openmui.example.rating:reading-list:examples/04-rating
    openmui.example.contrast:contrast:examples/05-contrast
    openmui.example.theming:theme-preview:examples/06-theming
    openmui.example.headless-and-agent:notes-agent:examples/07-headless-and-agent
    openmui.example.markdown-viewer:md-view:examples/08-markdown-viewer
    openmui.example.markdown-stream:md-stream:examples/09-markdown-stream
    openmui.example.space:space:examples/10-canvas-basics
    openmui.example.notes:notes:examples/12-notes
    openmui.showcase.style_gallery:omui-styles:showcase/style-gallery
    openmui.showcase.widget_breakout:omui-breakout:showcase/widget-breakout
    openmui.showcase.markdown_wall:omui-wall:showcase/markdown-wall
    openmui.showcase.fighting:omui-fight:showcase/fighting
    openmui.site:omui-site:site
)
packages=(-p openmui.cli)
for p in "${programs[@]}"; do packages+=(-p "${p%%:*}"); done
mcpp build "${packages[@]}" ${PROFILE:+--profile "$PROFILE"} > /dev/null

newest() { find "$PWD/target" -path "*/bin/*" -name "$1" -type f -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2; }
omui=$(newest omui)
out=$PWD/target/sweep
mkdir -p "$out"
dir=$(mktemp -d)
pid=
trap '[ -n "$pid" ] && kill "$pid" 2> /dev/null; rm -rf "$dir"' EXIT

failed=0
for p in "${programs[@]}"; do
    IFS=: read -r _ bin where <<< "$p"
    rm -f "$dir"/*
    exe=$(newest "$bin")
    (cd "$where" && exec "$exe" --size 1440x900 --serve --agent-input --agent-dir "$dir") > "$out/$bin.log" 2>&1 &
    pid=$!
    endpoint=
    for _ in $(seq 200); do
        endpoint=$(find "$dir" -name '*.endpoint.omui.json' | head -1)
        [ -n "$endpoint" ] && break
        sleep 0.1
    done
    if [ -z "$endpoint" ]; then
        echo "FAIL $bin: no endpoint (see $out/$bin.log)"
        failed=1
    else
        app=$(basename "$endpoint" .endpoint.omui.json)
        set +e
        "$omui" agent sweep "$app" --dir "$dir" > "$out/$bin.sweep.omui.jsonl"
        code=$?
        set -e
        summary=$(grep '"nodes"' "$out/$bin.sweep.omui.jsonl" | tail -1)
        if [ "$code" != 0 ]; then
            echo "FAIL $bin ($app): $summary"
            grep '"verdict":"failed"' "$out/$bin.sweep.omui.jsonl" || true
            failed=1
        else
            echo "ok   $bin ($app): $summary"
        fi
    fi
    kill "$pid" 2> /dev/null || true
    wait "$pid" 2> /dev/null || true
    pid=
done
[ "$failed" = 0 ] && echo "sweep: ok"
exit "$failed"
