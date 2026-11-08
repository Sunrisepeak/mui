#!/usr/bin/env python3
"""Side-by-side comparison reviews (Part 2 plan §2.4, D-135; rubric
`compare-v1`): each review dimension is our side against a similar
project's on the same scene, judged behind / par / ahead — no scores.

A comparison set (`.agents/docs/evaluation/compare/<subject>.compare.omui.toml`)
lists, per dimension, pairs of material: our picture and the competitor's of
the same scene (stills, process-frame strips; made by the subject's own
material scripts), and the question the pair answers.

  compare.py sheets SUBJECT
      each pair as one blinded sheet: the two pictures side by side, labelled
      A and B only, which side is ours drawn from a seeded stream; the key
      kept apart (target/compare/<subject>/key.json)

  compare.py judge SUBJECT [--agent claude] [--samples 3] [--dry-run]
      each sheet judged by a fresh agent session that has nothing of the
      project — an empty directory, the sheet, the question — answering A, B
      or even, three times a sheet (the majority; a tie is par); unblinded into ahead / par / behind; a dimension's verdict is
      its weakest pair's; one compare-v1 record a dimension in
      ledger.eval.omui.jsonl (evaluator "claude-blind")

  compare.py record SUBJECT DIMENSION VERDICT --by maintainer [--note TEXT]
      the maintainer's verdict, which decides (evaluator "maintainer")
"""
from __future__ import annotations

import argparse
import datetime
import json
import random
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SETS = ROOT / ".agents/docs/evaluation/compare"
LEDGER = ROOT / ".agents/docs/evaluation/ledger.eval.omui.jsonl"
OUT = ROOT / "target/compare"
VERDICTS = ("behind", "par", "ahead")


def load(subject: str) -> dict:
    path = SETS / f"{subject}.compare.omui.toml"
    if not path.exists():
        sys.exit(f"compare.py: no comparison set {path.relative_to(ROOT)}")
    return tomllib.loads(path.read_text(encoding="utf-8"))


def commit() -> str:
    try:
        return subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short=8", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def append(record: dict) -> None:
    with LEDGER.open("a", encoding="utf-8") as out:
        out.write(json.dumps(record, ensure_ascii=False, separators=(", ", ": ")) + "\n")


def compose(left: Path, right: Path, out: Path) -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import png  # tools/eval/png.py: no imaging library needed

    pictures = [png.read(left), png.read(right)]
    band, gap = 44, 24
    sheet = png.Picture(pictures[0].width + pictures[1].width + gap, max(p.height for p in pictures) + band)
    x = 0
    for label, picture in zip("AB", pictures):
        sheet.paste(picture, x, band)
        png.letter(sheet, label, x + 8, 8)
        x += picture.width + gap
    png.write(sheet, out)


def cmd_sheets(arguments: argparse.Namespace) -> int:
    spec = load(arguments.subject)
    stream = random.Random(f"{arguments.subject}:{spec.get('seed', 1)}")
    out = OUT / arguments.subject
    if out.exists():
        shutil.rmtree(out)
    key = []
    for dimension in spec["dimension"]:
        for index, pair in enumerate(dimension["pair"]):
            ours, theirs = ROOT / pair["ours"], ROOT / pair["theirs"]
            for side in (ours, theirs):
                if not side.exists():
                    sys.exit(f"compare.py: {side.relative_to(ROOT)} does not exist (make the material first: {spec.get('material', 'see the set')})")
            ours_left = stream.random() < 0.5
            sheet = out / f"{dimension['id']}-{index}.png"
            compose(ours if ours_left else theirs, theirs if ours_left else ours, sheet)
            key.append({"dimension": dimension["id"], "pair": index, "sheet": sheet.relative_to(ROOT).as_posix(), "ours": "A" if ours_left else "B",
                        "competitor": pair["competitor"], "question": pair.get("question", dimension["question"])})
    (out / "key.json").write_text(json.dumps(key, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(f"compare.py: {len(key)} sheets in {out.relative_to(ROOT)}")
    return 0


PROMPT = """You are judging two pictures of the same user-interface scene, side by side in one image: the left is labelled A, the right B. They come from two different products; you are not told which is which, and it must not matter.

The question: {question}

Look at the image file {sheet}. Judge only what the picture shows, as an expert designer would against the best work in the field: craft, detail, clarity, consistency, polish. If the two are equally good, or differently good with no clear winner, say "even".

Answer with one JSON object and nothing else:
{{"winner": "A" | "B" | "even", "confidence": "low" | "medium" | "high", "reasons": ["…", "…"]}}"""


def ask(agent: str, sheet: Path, question: str) -> dict:
    """One fresh session in an empty directory: the sheet copied in, nothing else."""
    with tempfile.TemporaryDirectory(prefix="omui-compare-") as empty:
        local = Path(empty) / "sheet.png"
        shutil.copy(sheet, local)
        prompt = PROMPT.format(question=question, sheet=local)
        if agent == "claude":
            command = ["claude", "-p", prompt, "--allowedTools", "Read", "--output-format", "json"]
        else:
            sys.exit(f"compare.py: unknown agent {agent}")
        done = subprocess.run(command, cwd=empty, capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=600)
    if done.returncode != 0:
        return {"unavailable": (done.stderr or done.stdout).strip()[:300]}
    text = json.loads(done.stdout).get("result", "") if done.stdout.strip().startswith("{") else done.stdout
    found = re.search(r"\{.*\}", text, re.S)
    if not found:
        return {"unavailable": f"no JSON in the answer: {text[:200]}"}
    try:
        answer = json.loads(found.group(0))
    except json.JSONDecodeError:
        return {"unavailable": f"not JSON: {found.group(0)[:200]}"}
    if answer.get("winner") not in ("A", "B", "even"):
        return {"unavailable": f"winner is {answer.get('winner')!r}"}
    return answer


def unblind(answer: dict, ours: str) -> str:
    if answer["winner"] == "even":
        return "par"
    return "ahead" if answer["winner"] == ours else "behind"


def cmd_judge(arguments: argparse.Namespace) -> int:
    key_file = OUT / arguments.subject / "key.json"
    if not key_file.exists():
        sys.exit(f"compare.py: no sheets for {arguments.subject} (compare.py sheets {arguments.subject})")
    key = json.loads(key_file.read_text(encoding="utf-8"))
    by_dimension: dict[str, list[dict]] = {}
    for entry in key:
        # Each sheet judged `--samples` times by fresh sessions; its verdict is
        # the majority (a tie: par) — one session's call varies at low confidence.
        answers = [a for a in (ask(arguments.agent, ROOT / entry["sheet"], entry["question"]) for _ in range(arguments.samples)) if "unavailable" not in a]
        if not answers:
            print(f"{entry['dimension']}#{entry['pair']}: unavailable")
            continue
        verdicts = [unblind(a, entry["ours"]) for a in answers]
        tally = {v: verdicts.count(v) for v in VERDICTS}
        top = max(tally.values())
        winners = [v for v in VERDICTS if tally[v] == top]
        verdict = winners[0] if len(winners) == 1 else "par"
        answer = next(a for a, v in zip(answers, verdicts) if v == verdict) if verdict in verdicts else answers[0]
        entry = {**entry, "answer": answer, "verdict": verdict, "votes": verdicts}
        by_dimension.setdefault(entry["dimension"], []).append(entry)
        print(f"{entry['dimension']}#{entry['pair']} vs {entry['competitor']}: {entry['verdict']} (votes {', '.join(verdicts)})")
    for dimension, entries in by_dimension.items():
        # A dimension is as good as its weakest pair: "every dimension at least
        # par" means every control, not most of them (Part 2 plan §2.4).
        verdict = min((entry["verdict"] for entry in entries), key=VERDICTS.index)
        record = {"date": datetime.date.today().isoformat(), "subject": arguments.subject, "evaluator": f"{arguments.agent}-blind", "rubric": "compare-v1",
                  "commit": commit(), "dimension": dimension, "verdict": verdict, "rule": "weakest pair",
                  "pairs": [{"competitor": e["competitor"], "verdict": e["verdict"], "confidence": e["answer"].get("confidence"),
                             "votes": e["votes"], "reasons": e["answer"].get("reasons", [])[:4], "sheet": e["sheet"]} for e in entries]}
        print(f"{arguments.subject}/{dimension}: {verdict}")
        if not arguments.dry_run:
            append(record)
    return 0


def cmd_record(arguments: argparse.Namespace) -> int:
    if arguments.verdict not in VERDICTS:
        sys.exit(f"compare.py: a verdict is one of {', '.join(VERDICTS)}")
    record = {"date": datetime.date.today().isoformat(), "subject": arguments.subject, "evaluator": arguments.by, "rubric": "compare-v1",
              "commit": commit(), "dimension": arguments.dimension, "verdict": arguments.verdict, "note": arguments.note or ""}
    append(record)
    print(json.dumps(record, ensure_ascii=False))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="name", required=True)
    sheets = commands.add_parser("sheets")
    sheets.add_argument("subject")
    judge = commands.add_parser("judge")
    judge.add_argument("subject")
    judge.add_argument("--agent", default="claude")
    judge.add_argument("--dry-run", action="store_true")
    judge.add_argument("--samples", type=int, default=3)
    record = commands.add_parser("record")
    record.add_argument("subject")
    record.add_argument("dimension")
    record.add_argument("verdict")
    record.add_argument("--by", default="maintainer")
    record.add_argument("--note")
    arguments = parser.parse_args()
    return {"sheets": cmd_sheets, "judge": cmd_judge, "record": cmd_record}[arguments.name](arguments)


if __name__ == "__main__":
    sys.exit(main())
