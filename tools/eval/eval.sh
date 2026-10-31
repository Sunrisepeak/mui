#!/usr/bin/env bash
# One evaluation (.agents/docs/evaluation): codex, read-only, scores a subject
# against the standard and the subject's top 3, with the subject's last record
# beside it. The report goes to .agents/docs/evaluation/reports/<date>-<subject>.md,
# its scores to ledger.eval.omui.jsonl.
#
#   tools/eval/eval.sh <subject> [IMAGE…]      subjects: .agents/docs/evaluation/subjects/*.md
#
# The evaluator uses what it scores. The programs the subject's **Run:** list
# names start headless, serving their agent endpoints with --agent-input, and
# codex operates them through openmui's MCP server (`omui mcp`): ui_capture
# to see, ui_input to click, drag, scroll and type as a person does (the real
# input path: hit testing, focus, gestures), ui_act and ui_invoke as an agent
# does (openas: actor, effects, answers), ui_wait and ui_clock for time. The
# subject's **Use:** list says what to do with them; the report says what
# worked, what did not, and where.
#
# EVAL_DATE overrides the date (default: today); EVAL_TIMEOUT the seconds codex
# may take (default 3600); EVAL_BIN the directory the programs are found under
# (default: the release build this makes of them); EVAL_EFFORT codex's
# reasoning effort (default high).
set -euo pipefail

# All of it read before any of it runs: the script may be edited while an evaluation runs.
main() {
cd "$(dirname "$0")/../.."
subject=${1:?usage: tools/eval/eval.sh <subject> [IMAGE…]}
shift
dir=.agents/docs/evaluation
spec=$dir/subjects/$subject.md
[ -f "$spec" ] || { echo "eval: no subject $spec" >&2; exit 2; }
date=${EVAL_DATE:-$(date +%Y-%m-%d)}
report=$dir/reports/$date-$subject.md
for n in $(seq 2 99); do [ -e "$report" ] || break; report=$dir/reports/$date-$subject-$n.md; done  # another the same day
ledger=$dir/ledger.eval.omui.jsonl
mkdir -p "$dir/reports"
touch "$ledger"

previous=$(python3 - "$ledger" "$subject" <<'PY'
import json, sys
last = None
for line in open(sys.argv[1], encoding="utf-8"):
    line = line.strip()
    if line:
        r = json.loads(line)
        if r.get("subject") == sys.argv[2]:
            last = r
print(json.dumps(last, ensure_ascii=False) if last else "none")
PY
)

# The programs to use, started for the evaluator: each line of **Run:** is
# "- `program args…` (package)", built here with --profile release.
mapfile -t runs < <(awk '/^\*\*Run:\*\*/{on=1; next} on && /^- `/{sub(/^- `/, ""); sub(/`.*$/, ""); print; next} on && NF==0{on=0}' "$spec")
mapfile -t packages < <(awk '/^\*\*Run:\*\*/{on=1; next} on && /^- `/{if (match($0, /`[^`]*` \(([^)]*)\)/)) {s=substr($0, RSTART, RLENGTH); sub(/^`[^`]*` \(/, "", s); sub(/\)$/, "", s); print s}; next} on && NF==0{on=0}' "$spec" | sort -u)
ep=$(mktemp -d /tmp/omui-eval-XXXXXX)
pids=()
cleanup() { for p in "${pids[@]}"; do kill "$p" 2> /dev/null || true; done; rm -rf "$ep"; }
trap cleanup EXIT
if [ ${#runs[@]} -gt 0 ] && [ -z "${EVAL_BIN:-}" ]; then
    build=(build --profile release -p openmui.cli)
    for p in "${packages[@]}"; do build+=(-p "$p"); done
    EVAL_BIN=$("${MCPP:-mcpp}" "${build[@]}" --message-format json 2> /dev/null | tail -1 |
               python3 -c 'import json, sys; print(json.load(sys.stdin)["data"]["configuration"]["outputDir"] + "/bin")') ||
        { echo "eval: the release build failed (mcpp ${build[*]})" >&2; exit 1; }
fi
bin_of() {  # the program of that name under EVAL_BIN
    find "${EVAL_BIN:-target}" -path "*/bin/*" -name "$1" -type f -perm -u+x -printf '%T@ %p\n' 2> /dev/null | sort -rn | head -1 | cut -d' ' -f2
}
omui=$(bin_of omui)
use=""
if [ ${#runs[@]} -gt 0 ]; then
    [ -n "$omui" ] || { echo "eval: no omui (mcpp build --profile release -p openmui.cli)" >&2; exit 2; }
    for line in "${runs[@]}"; do
        read -r -a words <<< "$line"
        program=$(bin_of "${words[0]}")
        [ -n "$program" ] || { echo "eval: no ${words[0]} built (mcpp build --profile release -p …)" >&2; exit 2; }
        before=$(ls "$ep" | grep -c endpoint || true)
        "$program" "${words[@]:1}" --serve --agent-input --agent-dir "$ep" > "$ep/${words[0]}.log" 2>&1 &
        pids+=($!)
        for _ in $(seq 300); do [ "$(ls "$ep" | grep -c endpoint || true)" -gt "$before" ] && break; sleep 0.1; done
        [ "$(ls "$ep" | grep -c endpoint || true)" -gt "$before" ] || { echo "eval: ${words[0]} did not start (see below)" >&2; cat "$ep/${words[0]}.log" >&2; exit 1; }
    done
    use=$(cat <<TXT

USE IT FIRST. The programs below are running now, headless, each serving its semantic tree; the openmui MCP tools (ui_apps, ui_snapshot,
ui_find, ui_capture, ui_input, ui_act, ui_invoke, ui_undo, ui_wait, ui_clock) operate them:
$(for line in "${runs[@]}"; do echo "- $line"; done)
Use the subject as its people would before you score it, and keep a use log:
- See with ui_capture (the window's pixels) and ui_snapshot (the tree: ids, roles, names, actions, bounds as @x,y wxh).
- Act as a person with ui_input: click, double_click, drag, wheel, key, text at what you SEE (coordinates from the capture, logical
  pixels); it goes through the real input path (hit testing, focus, gestures), so a control that does not answer a click is a defect a
  person meets. Its answer says what each press hit and which actions ran. Then capture again and check what you see changed as it should.
- Act as an agent with ui_act / ui_invoke (openas: the effect, the answer an irreversible action needs; never answer for the person).
- Let motion finish with ui_wait {settled: true} before judging a still; use ui_clock (pause, step) to look at motion frame by frame.
- Do every task the subject's Use list names, then explore on your own as a demanding first-time person would.
- Every program here is built with openmui, so what you meet of the framework, the engine and openas while using it is evidence about
  them too: the semantic tree (names, roles, values, actions, bounds against what is drawn), the agent tools, actors and effects,
  refusals and the answers an irreversible action asks for, input as the agent's own.
In the report, before the dimension table, a section "Use log": a table — task, what you did (the tool calls, briefly), what you expected,
what happened, pass/fail — and every defect found by using it (where, how to reproduce); then "Framework and openas feedback": what
worked, what was missing or wrong, what to add, as you met it. Mark evidence from use as "used". A dimension
about what a person meets (interaction, usability, onboarding, motion, performance felt) scored without use evidence is provisional and
at most 70.
TXT
)
fi

prompt=$(cat <<TXT
You are an independent, demanding evaluator for openmui (a C++ Modules UI framework whose core is one semantic tree rendered as pixels, accessibility, agent JSON and tests; this repository is the current directory — read AGENTS.md first). Score one subject by the standard below, against the top 3 of its field named in the subject. Read the code and files the subject names; do not guess. Attached images, when there are any, are the subject's current frames; their file names say what they are.

THE STANDARD (from .agents/docs/evaluation/README.md, sections 1–3; it is written in Chinese, answer in English):

$(sed -n '/^## 1\./,/^## 4\./p' "$dir/README.md" | sed '$d')

THE SUBJECT:

$(cat "$spec")

THE SUBJECT'S LAST RECORD (compare with it, dimension by dimension; "none" for a first evaluation):
$previous
$use

Write the report in English, under 2500 words, in the structure of section 3, the dimension keys exactly as the subject names them. Overall is the lowest dimension. Do not inflate: missing evidence earns no credit; mark scores that are inferred, not measured or seen.
TXT
)

args=(exec --sandbox read-only -c approval_policy=\"never\" -c "model_reasoning_effort=\"${EVAL_EFFORT:-high}\"" -o "$report")
if [ ${#runs[@]} -gt 0 ]; then
    args+=(-c "mcp_servers.openmui.command=\"$omui\""
           -c "mcp_servers.openmui.args=[\"mcp\",\"--dir\",\"$ep\",\"--actor\",\"agent:codex-evaluator\"]"
           -c "mcp_servers.openmui.tool_timeout_sec=300"
           -c 'mcp_servers.openmui.default_tools_approval_mode="approve"')  # openas decides, not codex's reviewer
fi
args+=("$prompt")
[ $# -gt 0 ] && args+=(-i "$@")
timeout "${EVAL_TIMEOUT:-3600}" codex "${args[@]}" < /dev/null > "$report.log" 2>&1 || { echo "eval: codex failed (see $report.log)" >&2; exit 1; }
# What the evaluator did with the programs: its tool calls, counted (the report's use log says what they found).
calls=$(grep -c '^mcp: openmui/.* (completed)' "$report.log" || true)
inputs=$(grep -c '^mcp: openmui/ui_input (completed)' "$report.log" || true)
rm -f "$report.log"

python3 - "$report" "$ledger" "$subject" "$date" "$(git rev-parse --short HEAD)" "${calls:-0}" "${inputs:-0}" <<'PY'
import json, re, sys
report, ledger, subject, date, commit, calls, inputs = sys.argv[1:]
text = open(report, encoding="utf-8").read()
blocks = re.findall(r"```json\s*(\{.*?\})\s*```", text, re.S)
if not blocks:
    sys.exit(f"eval: {report} ends with no scores block")
s = json.loads(blocks[-1])
dims = s.get("dimensions", {})
record = {"date": date, "subject": subject, "evaluator": "codex", "rubric": "v2", "commit": commit,
          "overall": s.get("overall", min(dims.values()) if dims else None),
          "mean": s.get("mean", round(sum(dims.values()) / len(dims), 1) if dims else None),
          "dimensions": dims, "report": report.split(".agents/docs/evaluation/")[-1],
          "used": {"tool_calls": int(calls), "inputs": int(inputs)}}
with open(ledger, "a", encoding="utf-8") as f:
    f.write(json.dumps(record, ensure_ascii=False) + "\n")
print(f"{subject}: overall {record['overall']}, mean {record['mean']} → {report}")
PY
}
main "$@"
exit
