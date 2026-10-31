#!/usr/bin/env bash
# Two codex agents play the fighting game against each other, by turns, and
# a third looks at the recording (.agents/docs/evaluation: the fighting
# game's use evidence, and the engine's and openas's).
#
#   tools/eval/duel.sh [P1 P2]      fighters by their files' names (default: kata mo)
#
# The game runs headless as `omui-fight --fight P1 P2 --turns N`: the match
# stands until both players have said what they hold for the next N frames
# (the stage's `turn` action), then plays them. Each player is a codex with
# openmui's MCP server (its own actor, agent:codex-p1 / agent:codex-p2): it
# reads the stage from the semantic tree, sees it with ui_capture, and plays.
# Meanwhile this script records every turn: the stage's state as the tree
# says it (duel.log.omui.jsonl) and its picture. Then the analyst — a third
# codex, read-only — gets contact sheets of the recording, the log and both
# players' notes, and writes the report.
#
# DUEL_TURN frames a turn (default 10); DUEL_TURNS at most this many (default
# 150); DUEL_TIMEOUT the seconds the players may take (default 3600);
# DUEL_ANALYSE a recording's directory to analyse again (no play); MCPP
# the mcpp to build with; EVAL_DATE the report's date.
set -euo pipefail

analyse() {
    # Contact sheets: every turn's picture, 24 a sheet.
    /usr/bin/python3 - "$work" <<'PY'
import glob, os, sys
from PIL import Image, ImageDraw
work = sys.argv[1]
shots = sorted(glob.glob(os.path.join(work, "turns", "*.png")))
for n in range(0, len(shots), 24):
    part = shots[n:n + 24]
    tiles = [Image.open(p).convert("RGB").resize((320, 180)) for p in part]
    sheet = Image.new("RGB", (6 * 320, 4 * 200), (12, 12, 16))
    d = ImageDraw.Draw(sheet)
    for i, (p, t) in enumerate(zip(part, tiles)):
        x, y = (i % 6) * 320, (i // 6) * 200
        sheet.paste(t, (x, y))
        d.text((x + 4, y + 182), "turn " + os.path.basename(p)[:-4], fill=(220, 220, 220))
    sheet.save(os.path.join(work, f"sheet-{n // 24:02}.png"))
PY

    # The analyst.
    local sheets=("$work"/sheet-*.png)
    local prompt
    prompt=$(cat <<TXT
Two AI agents just played a match of openmui's fighting game against each other, by turns of $frames frames: player 1 as $p1, player 2 as
$p2. You are the analyst. The attached contact sheets show the picture after each turn (turn numbers under each); the log of every turn's
state as the semantic tree says it is $work/duel.log.omui.jsonl; the players' own notes are $work/p1.md and $work/p2.md (missing if a
player stopped early). The game's code is showcase/fighting and mgame/gongfu (read AGENTS.md, showcase/fighting/README.md and the
fighting evaluation subject .agents/docs/evaluation/subjects/fighting.md).
Write a report in English, under 1500 words:
1. The match: who won and how — the decisive exchanges, by turn number.
2. The game as these players met it: readability (could they and you tell what happened from the tree and the picture), the moves'
   reach and feel, the turn-based play itself, fairness and balance between these two fighters, anything broken or odd (with turn numbers).
3. What it shows of the engine and of openas: deterministic simulation, the tree as an agent's view, actions and their answers.
4. The fixes that would matter most, in order.
TXT
)
    local args=(exec --sandbox read-only -c approval_policy=\"never\" -c 'model_reasoning_effort="high"' -o "$report" "$prompt")
    [ -e "${sheets[0]}" ] && args+=(-i "${sheets[@]}")  # after the prompt: -i takes every path that follows
    codex "${args[@]}" < /dev/null > "$work/analyst.log" 2>&1 || echo "duel: the analyst failed (see $work/analyst.log)" >&2
    {
        echo
        echo "## The players' notes"
        for me in 1 2; do
            echo; echo "### Player $me"; echo
            # Its last message (written at its end; else the last it said in its log).
            cat "$work/p$me.md" 2> /dev/null || awk '/^codex$/{buf=""; on=1; next} /^(mcp|exec|tokens used)/{on=0} on{buf=buf $0 "\n"} END{printf "%s", buf}' "$work/p$me.log" 2> /dev/null || echo "(none)"
        done
        echo
        echo "Recording: $seen turns of $frames frames; the log and the pictures were in $work."
    } >> "$report"
}

main() {
    cd "$(dirname "$0")/../.."
    local p1=${1:-kata} p2=${2:-mo}
    local frames=${DUEL_TURN:-10} limit=${DUEL_TURNS:-150}
    local date=${EVAL_DATE:-$(date +%Y-%m-%d)}
    local dir=.agents/docs/evaluation
    local report=$dir/reports/$date-fighting-duel-$p1-$p2.md
    local work=${DUEL_ANALYSE:-}
    local seen=0
    if [ -n "$work" ]; then  # analyse a recording made before
        seen=$(wc -l < "$work/duel.log.omui.jsonl")
        analyse
        return
    fi
    work=$(mktemp -d /tmp/omui-duel-XXXXXX)
    local bin
    bin=$("${MCPP:-mcpp}" build --profile release -p openmui.cli -p openmui.showcase.fighting --message-format json 2> /dev/null | tail -1 |
          python3 -c 'import json, sys; print(json.load(sys.stdin)["data"]["configuration"]["outputDir"] + "/bin")') ||
        { echo "duel: the release build failed" >&2; exit 1; }
    local omui fight
    omui=$(find "$bin" -name omui -type f -perm -u+x | head -1)
    fight=$(find "$bin" -name omui-fight -type f -perm -u+x | head -1)
    mkdir -p "$work/ep" "$work/turns"
    "$fight" --fight "$p1" "$p2" --turns "$frames" --quiet --serve --agent-dir "$work/ep" > "$work/game.log" 2>&1 &
    local game=$!
    local -a pids=("$game")
    trap 'for p in "${pids[@]}"; do kill "$p" 2> /dev/null || true; done' EXIT
    for _ in $(seq 300); do [ -e "$work/ep/org.openmui.fighting.endpoint.omui.json" ] && break; sleep 0.1; done

    # The players.
    for side in 0 1; do
        local me=$((side + 1)) them=$((2 - side))
        local prompt
        prompt=$(cat <<TXT
You are player $me (player index $side) in a fighting game match against another AI agent (player $them). Play to win.

The game (openmui's fighting game, gongfu) runs now; the openmui MCP tools reach it. The match is played BY TURNS: it stands still until
both players have said what they hold for the next $frames frames (1/6 s at 60 fps), then plays those frames.
- Read the stage: ui_snapshot {root: "main/stage"} — its value has frame, seconds, round, wins, and for each player: name, health, doing
  (the move or state), x and y (position and height; the stage is about 1920 wide), facing, actionable (whether it can act now), move
  {name, frame, of, phase: startup|active|recovery, left}, stun, hitstop, throw_protected, combo (hits it has taken), posture, meter; and
  turns {played, frames, waiting: [players still to say], last: what happened in the last turn — each move begun (what your pads were read
  as), hit, block, throw … by whom, with damage}.
  ui_capture shows the picture (look now and then: the fighters, the HUD, the effects).
- Act: ui_act {node: "main/stage", action: "turn", args: {player: $side, pad: PAD}}. PAD is numpad notation: a direction 1–9 (5 neutral,
  6 toward the right of the screen, 4 left, 2 down, 8 up, 3 down-right …) and buttons LP MP HP LK MK HK (e.g. "5LP", "2MK", "6HP",
  "5LP+LK"). Several pads, one a frame, the last held to the turn's end, make motions: "2,3,6LP" (a quarter circle toward the right and a
  light punch), "6,2,3HP" (a dragon punch), "4,1,2,3,6HK". Directions are on the screen, not relative to facing: check where you and the
  other player stand (x) to know forward and back. Hold back (away from the other) to guard, down-back to guard low.
- After your turn the answer says whether the other player has said theirs; if not, read the stage again (ui_snapshot) until turns.played
  grows, then decide your next turn. Do not wait idly for long: poll a few times a second.
- Your fighter's moves: read showcase/fighting/fighters/<name>.fighter.gongfu.toml (each [[move]]: name, input, damage …).
Play the whole match: keep taking turns until the stage says over, or after $limit turns of yours. Mix approaches: walk, jump, poke, guard,
throw (LP+LK near), specials, supers when your meter allows. Then, as your last message, write 5–10 sentences: what you tried, what
worked, what the game made easy or hard to read and to do (as a player), anything that looked or felt wrong, and the turn numbers where.
TXT
)
        codex exec --sandbox read-only -c approval_policy=\"never\" -c 'model_reasoning_effort="low"' \
            -c "mcp_servers.openmui.command=\"$omui\"" \
            -c "mcp_servers.openmui.args=[\"mcp\",\"--dir\",\"$work/ep\",\"--actor\",\"agent:codex-p$me\"]" \
            -c 'mcp_servers.openmui.tool_timeout_sec=120' -c 'mcp_servers.openmui.default_tools_approval_mode="approve"' \
            -o "$work/p$me.md" "$prompt" < /dev/null > "$work/p$me.log" 2>&1 &
        pids+=($!)
    done
    local players=("${pids[@]:1}")

    # The recording: each turn played, its state and its picture.
    seen=-1
    local started=$SECONDS
    while kill -0 "${players[0]}" 2> /dev/null || kill -0 "${players[1]}" 2> /dev/null; do
        local state
        state=$("$omui" agent snapshot org.openmui.fighting --root main/stage --dir "$work/ep" 2> /dev/null | head -1) || true
        local played over
        read -r played over < <(python3 -c 'import json, sys
try:
    d = json.loads(sys.stdin.read())["data"]["nodes"][0]["value"]
    print(d["turns"]["played"], 1 if d.get("over") else 0)
except Exception:
    print(-1, 0)' <<< "$state")
        if [ "$played" -gt "$seen" ]; then
            seen=$played
            printf '%s\n' "$(python3 -c 'import json, sys; print(json.dumps(json.loads(sys.stdin.read())["data"]["nodes"][0]["value"]))' <<< "$state")" >> "$work/duel.log.omui.jsonl"
            "$omui" agent capture org.openmui.fighting --out "$work/turns/$(printf %04d "$played").png" --dir "$work/ep" > /dev/null 2>&1 || true
        fi
        if [ "$over" = 1 ] || [ "$seen" -ge "$limit" ] || [ $((SECONDS - started)) -gt "${DUEL_TIMEOUT:-3600}" ]; then break; fi
        sleep 0.1
    done
    sleep 20  # the players' last words
    # What the round was played with, and where it led (a replay's check).
    "$omui" agent act org.openmui.fighting main/stage inputs --dir "$work/ep" > "$work/inputs.omui.jsonl" 2> /dev/null || true
    for p in "${players[@]}"; do kill "$p" 2> /dev/null || true; done

    analyse
    echo "duel: $seen turns → $report (recording in $work)"
}
main "$@"
exit
