#!/usr/bin/env bash
# One evaluation (.agents/docs/evaluation): codex, read-only, scores a subject
# against the standard and the subject's top 3, with the subject's last record
# beside it. The report goes to .agents/docs/evaluation/reports/<date>-<subject>.md,
# its scores to ledger.eval.omui.jsonl.
#
#   tools/eval/eval.sh <subject> [IMAGE…]      subjects: .agents/docs/evaluation/subjects/*.md
#
# EVAL_DATE overrides the date (default: today); EVAL_TIMEOUT the seconds codex may take (default 3600).
set -euo pipefail
cd "$(dirname "$0")/../.."
subject=${1:?usage: tools/eval/eval.sh <subject> [IMAGE…]}
shift
dir=.agents/docs/evaluation
spec=$dir/subjects/$subject.md
[ -f "$spec" ] || { echo "eval: no subject $spec" >&2; exit 2; }
date=${EVAL_DATE:-$(date +%Y-%m-%d)}
report=$dir/reports/$date-$subject.md
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

prompt=$(cat <<TXT
You are an independent, demanding evaluator for openmui (a C++ Modules UI framework whose core is one semantic tree rendered as pixels, accessibility, agent JSON and tests; this repository is the current directory — read AGENTS.md first). Score one subject by the standard below, against the top 3 of its field named in the subject. Read the code and files the subject names; do not guess. Attached images, when there are any, are the subject's current frames; their file names say what they are.

THE STANDARD (from .agents/docs/evaluation/README.md, sections 1–3; it is written in Chinese, answer in English):

$(sed -n '/^## 1\./,/^## 4\./p' "$dir/README.md" | sed '$d')

THE SUBJECT:

$(cat "$spec")

THE SUBJECT'S LAST RECORD (compare with it, dimension by dimension; "none" for a first evaluation):
$previous

Write the report in English, under 2500 words, in the structure of section 3, the dimension keys exactly as the subject names them. Overall is the lowest dimension. Do not inflate: missing evidence earns no credit; mark scores that are inferred, not measured or seen.
TXT
)

args=(exec --sandbox read-only -o "$report" "$prompt")
[ $# -gt 0 ] && args+=(-i "$@")
timeout "${EVAL_TIMEOUT:-3600}" codex "${args[@]}" < /dev/null > "$report.log" 2>&1 || { echo "eval: codex failed (see $report.log)" >&2; exit 1; }
rm -f "$report.log"

python3 - "$report" "$ledger" "$subject" "$date" "$(git rev-parse --short HEAD)" <<'PY'
import json, re, sys
report, ledger, subject, date, commit = sys.argv[1:]
text = open(report, encoding="utf-8").read()
blocks = re.findall(r"```json\s*(\{.*?\})\s*```", text, re.S)
if not blocks:
    sys.exit(f"eval: {report} ends with no scores block")
s = json.loads(blocks[-1])
dims = s.get("dimensions", {})
record = {"date": date, "subject": subject, "evaluator": "codex", "rubric": "v1", "commit": commit,
          "overall": s.get("overall", min(dims.values()) if dims else None),
          "mean": s.get("mean", round(sum(dims.values()) / len(dims), 1) if dims else None),
          "dimensions": dims, "report": report.split(".agents/docs/evaluation/")[-1]}
with open(ledger, "a", encoding="utf-8") as f:
    f.write(json.dumps(record, ensure_ascii=False) + "\n")
print(f"{subject}: overall {record['overall']}, mean {record['mean']} → {report}")
PY
