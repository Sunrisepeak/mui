#!/usr/bin/env python3
"""A real LLM does animation work without a GUI (convergence plan §10.3).

X
skill, the document reference and the `hanim` command — writes
`animation.omui.json` in an empty directory of its own and verifies it with
the command. The grader then checks the document as the brief says, through
`hanim sample` (numbers at moments, never codex's word). Every trial is
appended to the ledger (animation.eval.omui.jsonl) with its commit, model,
time and outcome; the run's success rate is printed and recorded. An agent
that cannot run (a quota, a login) is recorded as unavailable, never as a
failure of the work and never as a pass.

    tools/eval/animate.py                       every brief, once
    tools/eval/animate.py --trials 2 --only bounce,spring
    tools/eval/animate.py --grade DIR           grade one directory (a trial kept)
"""
import argparse
import concurrent.futures
import datetime
import json
import math
import os
import pathlib
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
BRIEFS = HERE / "animation-briefs.omui.json"
LEDGER = ROOT / ".agents/docs/evaluation/animation.eval.omui.jsonl"


def hanim_binary():
    found = sorted((p for p in (ROOT / "target").glob("*/*/bin/hanim.studio/hanim") if p.is_file()), key=lambda p: p.stat().st_mtime)
    if not found:
        sys.exit("animate: build the tool first: tools/dev/capped.sh mcpp build -p hanim.studio")
    return found[-1]


def run(hanim, *args):
    out = subprocess.run([str(hanim), *map(str, args)], capture_output=True, text=True, timeout=120)
    try:
        return out.returncode, json.loads(out.stdout)
    except json.JSONDecodeError:
        return out.returncode, {"ok": False, "raw": out.stdout + out.stderr}


# ── checks: each a name and a function of what `hanim sample` says ──────────

class Course:
    """The document sampled: its plan, and each object's state at many moments."""

    def __init__(self, hanim, document, steps=600):
        code, checked = run(hanim, "check", document)
        self.ok = code == 0 and checked.get("ok")
        self.problems = checked.get("problems", [])
        if not self.ok:
            return
        self.plan = checked["plan"]
        self.duration = self.plan["duration"] if isinstance(self.plan["duration"], (int, float)) else 10.0
        self.times = [self.duration * index / steps for index in range(steps + 1)]
        args = ["sample", document]
        for t in self.times:
            args += ["--at", f"{t:.6f}"]
        code, sampled = run(hanim, *args)
        self.ok = code == 0 and sampled.get("ok")
        self.samples = sampled.get("samples", []) if self.ok else []
        self.document = json.loads(pathlib.Path(document).read_text())

    def object(self, index, name):
        return next((o for o in self.samples[index]["objects"] if o["id"] == name), None)

    def series(self, name, key):
        return [(s["time"], (self.object(i, name) or {}).get(key)) for i, s in enumerate(self.samples)]

    def at(self, moment, name):
        index = 0 if moment == "start" else len(self.samples) - 1 if moment == "end" else min(
            range(len(self.times)), key=lambda i: abs(self.times[i] - moment))
        return self.object(index, name) or {}


def near(a, b, tolerance):
    return a is not None and b is not None and abs(a - b) <= tolerance


def rgba(text):
    text = (text or "#00000000").lstrip("#")
    return [int(text[i:i + 2], 16) for i in range(0, 8, 2)]


def bounces(values, ground=280):
    """How many times it leaves the ground again after touching it (its bottom edge's samples)."""
    count, down = 0, False
    for value in values:
        if value is None:
            continue
        if value >= ground - 0.5:
            down = True
        elif down and value < ground - 1:
            count += 1
            down = False
    return count


def grade(course, brief):
    """The brief's checks: [(name, passed, detail)]."""
    results = []

    def check(name, passed, detail=""):
        results.append((name, bool(passed), detail))

    check("the document checks", course.ok, json.dumps(course.problems)[:300])
    if not course.ok:
        return results
    kind = brief["id"]
    if kind == "bounce":
        start, end = course.at("start", "ball"), course.at("end", "ball")
        check("the ball starts at (100, 60)", near((start.get("position") or [None])[0], 100, 1) and near(start["position"][1], 60, 1), start.get("position"))
        bottoms = [None if b is None else b[3] for _, b in course.series("ball", "bounds")]
        check("it bounces at least twice", bounces(bottoms) >= 2, f"{bounces(bottoms)} bounces")
        check("it rests on the ground (bottom at 280) at the end", near((end.get("bounds") or [0, 0, 0, None])[3], 280, 2), end.get("bounds"))
        check("it ends at x = 380", near((end.get("position") or [None])[0], 380, 2), end.get("position"))
        xs = [p[0] for _, p in course.series("ball", "position") if p]
        moves = next((i for i, x in enumerate(xs) if abs(x - xs[0]) > 0.5), None)
        check("it rolls only after it has landed for good",
              moves is not None and all(b is not None and b >= 279.5 for b in bottoms[moves:]), f"moves at sample {moves}")
    elif kind == "morph":
        start, end = course.at("start", "square"), course.at("end", "square")
        b = start.get("bounds") or [0, 0, 0, 0]
        check("a blue 80 × 80 square at the centre to begin", near(b[2] - b[0], 80, 1) and near(b[3] - b[1], 80, 1) and rgba(start.get("fill"))[2] > 150, start)
        e = end.get("bounds") or [0, 0, 0, 0]
        check("it ends as an 80-wide shape at the same place", near(e[2] - e[0], 80, 2) and near((e[0] + e[2]) / 2, 240, 2) and near((e[1] + e[3]) / 2, 160, 2), e)
        r, g, bl, _ = rgba(end.get("fill"))
        check("it ends yellow", r > 200 and g > 170 and bl < 120, end.get("fill"))
        steps = json.dumps(course.document["program"])
        check("it morphs (a morph step into a circle)", '"morph"' in steps and any("circle" in o.get("shape", {}) for o in course.document["objects"]), "")
        scales = [s[0] for _, s in course.series("square", "scale") if s]
        check("it pulses after the morph", max(scales) > 1.02 and near(scales[-1], 1, 0.01), f"max scale {max(scales):.3f}")
    elif kind == "stagger":
        rises = []
        for n in range(1, 6):
            ys = course.series(f"dot{n}", "position")
            if not all(p for _, p in ys):
                check(f"dot{n} is drawn throughout", False)
                return results
            first = next((t for t, p in ys if p[1] < 155), None)
            rises.append(first)
            check(f"dot{n} starts at ({80 * n}, 160)", near(ys[0][1][0], 80 * n, 1) and near(ys[0][1][1], 160, 1), ys[0][1])
            check(f"dot{n} rises to 100", min(p[1] for _, p in ys) < 101, min(p[1] for _, p in ys))
            check(f"dot{n} returns to 160", near(ys[-1][1][1], 160, 1), ys[-1][1])
        ordered = all(r is not None for r in rises) and all(b - a > 0.05 for a, b in zip(rises, rises[1:]))
        check("they rise one after another, about 0.1 s apart", ordered and all(near(b - a, 0.1, 0.03) for a, b in zip(rises, rises[1:])), rises)
    elif kind == "spring":
        scales = [s[0] for _, s in course.series("card", "scale") if s]
        rotations = [r for _, r in course.series("card", "rotation_degrees") if r is not None]
        start = course.at("start", "card")
        b = start.get("bounds") or [0, 0, 0, 0]
        check("a white card, 160 × 100 at scale 0.5 to begin", near(scales[0], 0.5, 0.01) and near(b[2] - b[0], 80, 1.5) and rgba(start.get("fill"))[:3] == [255, 255, 255], start)
        check("it overshoots 1 on a spring", max(scales) > 1.02, f"max {max(scales):.3f}")
        check("it settles at 1", near(scales[-1], 1, 0.005), scales[-1])
        check("it turns 10 degrees", near(max(rotations), 10, 0.5), max(rotations))
        check("and back", near(rotations[-1], 0, 0.5), rotations[-1])
        top = scales.index(max(scales))
        turned = next((i for i, r in enumerate(rotations) if abs(r) > 0.5), len(rotations))
        check("the turn comes after the spring", turned > top, f"turn at sample {turned}, peak at {top}")
    elif kind == "fade":
        series = course.series("bar", "fill")
        alphas = [rgba(f)[3] for _, f in series]
        ys = [p[1] for _, p in course.series("bar", "position")]
        check("it starts invisible", alphas[0] == 0, alphas[0])
        check("it ends opaque", alphas[-1] == 255, alphas[-1])
        check("it rises from y = 200 to y = 160", near(ys[0], 200, 1) and near(ys[-1], 160, 1), (ys[0], ys[-1]))
        middle = [i for i, a in enumerate(alphas) if 40 < a < 215]
        check("it fades and rises at the same time", any(170 < ys[i] < 195 for i in middle), f"{len(middle)} half-faded samples")
        check("the fade takes about 0.5 s", near(sum(1 for a in alphas if 0 < a < 255) * course.duration / (len(alphas) - 1), 0.5, 0.12), "")
    else:
        check("a known brief", False, kind)
    return results


# ── a trial: codex writes the document, the grader checks it ───────────────

def prompt(brief, hanim):
    skill = (ROOT / "modules/projects/hanim/skill.omui.md").read_text()
    reference = (ROOT / "modules/projects/hanim/studio/README.md").read_text()
    example = (ROOT / "modules/projects/hanim/studio/examples/bounce.animation.omui.json").read_text()
    return f"""You are making an animation without a GUI, with hanim's animation tool.

Write `animation.omui.json` in the current directory for this brief, then verify it with the tool until it does what the brief asks. Use the tool's own answers (check, sample, render) as evidence; look at a rendered strip if you can. Do not ask questions; finish the work.

The tool is `{hanim}` (commands: check FILE, sample FILE --at T …, render FILE --out PREFIX --frames N).

THE BRIEF ({brief['id']}):
{brief['text']}

HANIM'S SKILL:
{skill}

THE DOCUMENT REFERENCE:
{reference}

AN EXAMPLE DOCUMENT:
{example}

When done, reply with one line: done, or what you could not do and why."""


# What says the agent itself could not run (a quota, a login): unavailable, not a failure of the work.
UNAVAILABLE = ("usage limit", "rate limit", "Invalid API key", "Please run /login", "not logged in", "credit balance is too low")


def trial(brief, number, hanim, keep, agent, model, effort, timeout):
    work = pathlib.Path(tempfile.mkdtemp(prefix=f"animate-{brief['id']}-{number}-", dir=keep))
    started = time.time()
    if agent == "codex":
        args = ["codex", "exec", "--sandbox", "workspace-write", "--skip-git-repo-check", "-C", str(work),
                "-c", 'approval_policy="never"', "-c", f'model_reasoning_effort="{effort}"', "-o", str(work / "answer.md")]
        if model:
            args += ["-m", model]
        args.append(prompt(brief, hanim))
    else:  # Claude Code, headless, in the trial's directory
        args = ["claude", "-p", prompt(brief, hanim), "--output-format", "text", "--permission-mode", "bypassPermissions",
                "--disallowedTools", "WebFetch", "WebSearch", "Agent", "--model", model or "sonnet"]
    try:
        out = subprocess.run(args, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=timeout, cwd=work)
        log = out.stdout + out.stderr
        if agent == "claude":
            (work / "answer.md").write_text(out.stdout)
    except subprocess.TimeoutExpired:
        log = "timeout"
    (work / f"{agent}.log").write_text(log)
    seconds = round(time.time() - started)
    document = work / "animation.omui.json"
    answer = (work / "answer.md").read_text().strip() if (work / "answer.md").exists() else ""
    if not document.exists() and any(text in log for text in UNAVAILABLE):
        return {"brief": brief["id"], "trial": number, "passed": None, "unavailable": next(t for t in UNAVAILABLE if t in log),
                "seconds": seconds, "checks": [], "answer": answer[:500], "directory": str(work)}
    if document.exists():
        results = grade(Course(hanim, document), brief)
    else:
        results = [("a document was written", False, "")]
    return {"brief": brief["id"], "trial": number, "passed": all(p for _, p, _ in results), "seconds": seconds,
            "checks": [{"check": n, "passed": p, "detail": str(d)[:300]} for n, p, d in results],
            "answer": answer[:500], "directory": str(work)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trials", type=int, default=1)
    parser.add_argument("--only", default="")
    parser.add_argument("--parallel", type=int, default=3)
    parser.add_argument("--agent", choices=["claude", "codex"], default="claude")
    parser.add_argument("--model", default="", help="the agent's model (claude: sonnet by default)")
    parser.add_argument("--effort", default="medium", help="codex's reasoning effort")
    parser.add_argument("--timeout", type=int, default=1500)
    parser.add_argument("--keep", default=str(ROOT / "target/eval/animate"))
    parser.add_argument("--grade", help="grade one trial directory (brief from its name)")
    parser.add_argument("--no-ledger", action="store_true")
    options = parser.parse_args()
    hanim = hanim_binary()
    briefs = json.loads(BRIEFS.read_text())["briefs"]
    if options.grade:
        directory = pathlib.Path(options.grade)
        brief = next(b for b in briefs if directory.name.startswith(f"animate-{b['id']}-"))
        for name, passed, detail in grade(Course(hanim, directory / "animation.omui.json"), brief):
            print(f"{'ok  ' if passed else 'FAIL'} {name}  {detail}")
        return 0
    if options.only:
        briefs = [b for b in briefs if b["id"] in options.only.split(",")]
    pathlib.Path(options.keep).mkdir(parents=True, exist_ok=True)
    jobs = [(b, n) for b in briefs for n in range(1, options.trials + 1)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=options.parallel) as pool:
        results = list(pool.map(lambda job: trial(job[0], job[1], hanim, options.keep, options.agent, options.model, options.effort,
                                                  options.timeout), jobs))
    commit = subprocess.run(["git", "rev-parse", "--short=8", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    version = subprocess.run([options.agent, "--version"], capture_output=True, text=True).stdout.strip()
    model = options.model or ("sonnet" if options.agent == "claude" else "default")
    date = datetime.date.today().isoformat()
    for r in results:
        state = "unavailable" if r["passed"] is None else "pass" if r["passed"] else "FAIL"
        print(f"{state}  {r['brief']} #{r['trial']}  {r['seconds']} s  {r['directory']}" + (f"  ({r['unavailable']})" if r["passed"] is None else ""))
        for c in r["checks"]:
            if not c["passed"]:
                print(f"        ✗ {c['check']}: {c['detail']}")
    ran = [r for r in results if r["passed"] is not None]
    passed = sum(bool(r["passed"]) for r in ran)
    print(f"animate: {passed} of {len(ran)} passed ({100 * passed / max(1, len(ran)):.0f}%)"
          + (f"; {len(results) - len(ran)} unavailable (not counted)" if len(ran) < len(results) else ""))
    if not ran:
        print("animate: the agent could not run; nothing recorded")
        return 1
    if not options.no_ledger:
        with LEDGER.open("a") as ledger:
            for r in results:
                ledger.write(json.dumps({"date": date, "commit": commit, "agent": options.agent, "agent_version": version, "model": model,
                                         **{k: v for k, v in r.items() if k != "directory"}}, ensure_ascii=False) + "\n")
            ledger.write(json.dumps({"date": date, "commit": commit, "agent": options.agent, "model": model, "summary": True,
                                     "trials": len(ran), "passed": passed, "unavailable": len(results) - len(ran),
                                     "briefs": sorted({r["brief"] for r in results})}) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
