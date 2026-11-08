#!/usr/bin/env python3
"""The lead ledger (Part 2 plan §2, D-135): our side against the strongest
competitor, measured on one machine in the same job, judged by ratio.

  lead.py sample --series NAME [--series-field F] --round K --out FILE -- COMMAND...
      run COMMAND, tag each JSON line it prints with the series and the
      round, append the samples to FILE

  lead.py verdict SAMPLES [--metrics FILE] [--ledger FILE] [--dry-run]
      every measured metric of the list: the ratio a round (theirs ÷ ours
      when lower is better), its median and a 95 % bootstrap interval, the
      strongest competitor (the lowest median ratio), the verdict from the
      interval's lower end — an interval across a threshold takes the worse
      tier; one ledger line a metric

  lead.py capability --project P --feature F --test PATH
                     --competitor NAME=SUPPORT... [--ledger FILE]
      a capability against competitors (none | partial | app-defined |
      full): far_ahead when none has it, ahead when some have it only in
      part or by the application's own code, par when one has it whole

  lead.py summary [--metrics FILE] [--features FILE] [--ledger FILE]
      the latest verdict of every metric and feature, as Markdown

  lead.py check [ROOT]
      the features list against the ledger (lint `lead`); prints problems,
      exits 1 if any
"""
from __future__ import annotations

import argparse
import datetime
import json
import random
import socket
import statistics
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
METRICS = ROOT / "tests/bench/competitors/lead.metrics.omui.toml"
FEATURES = ROOT / ".agents/docs/evaluation/lead.features.omui.toml"
LEDGER = ROOT / ".agents/docs/evaluation/lead.ledger.omui.jsonl"

# Ratio thresholds (§2.2): r = ours ÷ the strongest competitor, larger better.
BEHIND, AHEAD, FAR = 0.95, 1.2, 10.0
TIERS = ("behind", "par", "ahead", "far_ahead")
SUPPORT = ("none", "partial", "app-defined", "full")
STATUSES = ("planned", "evidence", "claimed")
LINES = ("use-safely", "build-reliably", "together", "craft", "foundation")
PROJECTS = ("omui", "omui.components", "hanim", "xrender", "xengine", "gongfu", "spec")


def tier(ratio: float) -> str:
    if ratio >= FAR:
        return "far_ahead"
    if ratio >= AHEAD:
        return "ahead"
    if ratio >= BEHIND:
        return "par"
    return "behind"


def ratios(ours: list[float], theirs: list[float], better: str) -> list[float]:
    """One ratio a round, larger meaning better for us."""
    pairs = zip(ours, theirs)
    if better == "lower":
        return [their / our for our, their in pairs if our > 0]
    return [our / their for our, their in pairs if their > 0]


def interval(values: list[float], seed: int = 20261108, resamples: int = 4000) -> tuple[float, float]:
    """The median's 95 % bootstrap interval — seeded, so a rerun says the same."""
    if len(values) < 2:
        return (values[0], values[0]) if values else (0.0, 0.0)
    stream = random.Random(seed)
    medians = sorted(statistics.median(stream.choices(values, k=len(values))) for _ in range(resamples))
    return medians[int(0.025 * resamples)], medians[int(0.975 * resamples) - 1]


def load_toml(path: Path) -> dict:
    return tomllib.loads(path.read_text(encoding="utf-8"))


def read_jsonl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def append(path: Path, records: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as out:
        for record in records:
            out.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")


def commit() -> str:
    try:
        return subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short=8", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def today() -> str:
    return datetime.date.today().isoformat()


# --- sample ----------------------------------------------------------------

def series_of(line: dict, default: str) -> list[tuple[str, dict]]:
    """A program's JSON line as (series, sample) pairs. The vector comparison
    prints one line a renderer and one comparing the pictures; the latter
    becomes an error sample for each renderer against the reference."""
    out = []
    renderer = line.get("renderer")
    if renderer:
        out.append((renderer.split(" ")[0], line))
    for name, value in line.items():
        if isinstance(value, dict) and " against " in name and "mean_difference" in value:
            subject, reference = name.split(" against ")
            if subject.startswith("its SVG"):
                continue
            out.append((subject, {"scene": line.get("scene"), f"error_vs_{reference}": value["mean_difference"]}))
    if not out:
        out.append((default, line))
    return out


def cmd_sample(arguments: argparse.Namespace) -> int:
    if not arguments.command:
        print("lead.py sample: no command", file=sys.stderr)
        return 2
    done = subprocess.run(arguments.command, capture_output=True, text=True)
    if done.returncode != 0:
        sys.stderr.write(done.stderr)
        return done.returncode
    records = []
    for text in done.stdout.splitlines():
        text = text.strip()
        if not text.startswith("{"):
            continue
        line = json.loads(text)
        pairs = [(str(line[arguments.series_field]), line)] if arguments.series_field and arguments.series_field in line else series_of(line, arguments.series)
        for series, sample in pairs:
            record = {"series": series, "round": arguments.round, "key": sample.get("scene") or arguments.key}
            record.update({name: value for name, value in sample.items() if isinstance(value, (int, float)) and not isinstance(value, bool)})
            records.append(record)
    if not records:
        print(f"lead.py sample: {arguments.series} printed no JSON line", file=sys.stderr)
        return 1
    append(Path(arguments.out), records)
    return 0


# --- verdict ---------------------------------------------------------------

def series_values(samples: list[dict], series: str, key: str, field: str) -> dict[int, float]:
    values = {}
    for sample in samples:
        if sample["series"] == series and sample.get("key") == key and field in sample:
            values[sample["round"]] = float(sample[field])
    return values


def judge(metric: dict, samples: list[dict]) -> dict | None:
    key, field, better = metric["key"], metric["field"], metric["better"]
    ours = series_values(samples, metric["ours"], key, field)
    if not ours:
        return None
    candidates = []
    for name in metric["competitors"]:
        theirs = series_values(samples, name, key, field)
        rounds = sorted(set(ours) & set(theirs))
        if not rounds:
            continue
        each = ratios([ours[r] for r in rounds], [theirs[r] for r in rounds], better)
        if not each:
            continue
        low, high = interval(each)
        candidates.append({"name": name, "median": statistics.median(theirs[r] for r in rounds), "ratio": statistics.median(each),
                           "ci": [round(low, 4), round(high, 4)], "rounds": len(rounds)})
    if not candidates:
        return None
    best = min(candidates, key=lambda candidate: candidate["ratio"])
    our_values = [ours[r] for r in sorted(ours)]
    return {
        "kind": "metric", "date": today(), "project": metric["project"], "metric": metric["id"], "better": better,
        "core": metric.get("core", True),
        "ours": {"name": metric["ours"], "median": round(statistics.median(our_values), 5)},
        "best": {"name": best["name"], "median": round(best["median"], 5)},
        "others": [{"name": c["name"], "median": round(c["median"], 5), "ratio": round(c["ratio"], 4)} for c in candidates if c is not best],
        "ratio": round(best["ratio"], 4), "ci": best["ci"], "rounds": best["rounds"],
        "verdict": tier(best["ci"][0]),
        "machine": f"{socket.gethostname()}-{__import__('os').cpu_count()}t", "commit": commit(), "found_by": None,
    }


def latest(ledger: list[dict], kind: str, name_field: str) -> dict[str, dict]:
    out = {}
    for record in ledger:
        if record.get("kind") == kind:
            out[record[name_field]] = record
    return out


def cmd_verdict(arguments: argparse.Namespace) -> int:
    metrics = load_toml(Path(arguments.metrics))["metric"]
    samples = read_jsonl(Path(arguments.samples))
    ledger_path = Path(arguments.ledger)
    previous = latest(read_jsonl(ledger_path), "metric", "metric")
    records = []
    for metric in metrics:
        status = metric.get("status", "measured")
        if status == "pending_hardware":
            if previous.get(metric["id"], {}).get("verdict") != "pending_hardware":
                records.append({"kind": "metric", "date": today(), "project": metric["project"], "metric": metric["id"],
                                "better": metric["better"], "core": metric.get("core", True), "verdict": "pending_hardware",
                                "needs": metric.get("needs", ""), "commit": commit()})
            continue
        if status != "measured":
            continue
        record = judge(metric, samples)
        if record is None:
            print(f"lead.py: {metric['id']}: no paired samples", file=sys.stderr)
            continue
        records.append(record)
    for record in records:
        if record["verdict"] == "pending_hardware":
            print(f"{record['metric']:<44} pending_hardware ({record['needs']})")
        else:
            print(f"{record['metric']:<44} {record['ratio']:>8.3f}× [{record['ci'][0]:.3f}, {record['ci'][1]:.3f}] "
                  f"against {record['best']['name']:<12} {record['verdict']}")
    if not arguments.dry_run:
        append(ledger_path, records)
    return 0


# --- capability ------------------------------------------------------------

def capability_verdict(supports: list[str]) -> str:
    if any(support == "full" for support in supports):
        return "par"
    if all(support == "none" for support in supports):
        return "far_ahead"
    return "ahead"


def cmd_capability(arguments: argparse.Namespace) -> int:
    competitors = []
    for item in arguments.competitor:
        name, _, support = item.partition("=")
        if support not in SUPPORT:
            print(f"lead.py capability: {item}: support is one of {', '.join(SUPPORT)}", file=sys.stderr)
            return 2
        competitors.append({"name": name, "support": support})
    if not (ROOT / arguments.test).exists():
        print(f"lead.py capability: {arguments.test} does not exist", file=sys.stderr)
        return 2
    record = {"kind": "capability", "date": today(), "project": arguments.project, "feature": arguments.feature,
              "about": arguments.about, "test": arguments.test, "competitors": competitors,
              "verdict": capability_verdict([c["support"] for c in competitors]), "commit": commit()}
    append(Path(arguments.ledger), [record])
    print(json.dumps(record, ensure_ascii=False))
    return 0


# --- summary ---------------------------------------------------------------

def cmd_summary(arguments: argparse.Namespace) -> int:
    metrics = load_toml(Path(arguments.metrics))["metric"]
    features = load_toml(Path(arguments.features))["feature"]
    ledger = read_jsonl(Path(arguments.ledger))
    measured = latest(ledger, "metric", "metric")
    capabilities: dict[str, list[dict]] = {}
    for record in ledger:
        if record.get("kind") in ("capability", "task"):
            capabilities.setdefault(record["feature"], []).append(record)
    lines = ["# 领先度（lead）", "", f"由 `tools/eval/lead.py summary` 从台账生成（{today()}）。判定规则见第二部分方案 §2.2。", "",
             "## 指标", "", "| 项目 | 指标 | 我方 | 最强竞品 | 比值 [95% 区间] | 判定 |", "|---|---|---|---|---|---|"]
    for metric in metrics:
        record = measured.get(metric["id"])
        status = metric.get("status", "measured")
        if record and record["verdict"] != "pending_hardware":
            lines.append(f"| {metric['project']} | {metric['id']} | {record['ours']['name']} {record['ours']['median']:g} | "
                         f"{record['best']['name']} {record['best']['median']:g} | {record['ratio']:.2f}× [{record['ci'][0]:.2f}, {record['ci'][1]:.2f}] | {record['verdict']} |")
        else:
            verdict = "pending_hardware" if status == "pending_hardware" else ("planned" if status == "planned" else "未测")
            lines.append(f"| {metric['project']} | {metric['id']} | — | {', '.join(metric['competitors'])} | — | {verdict} |")
    lines += ["", "## 标志性特性", "", "| 项目 | 特性 | 主线 | 状态 | 对照判定 |", "|---|---|---|---|---|"]
    for feature in features:
        records = capabilities.get(feature["id"], [])
        verdicts = ", ".join(sorted({r["verdict"] for r in records})) or "—"
        lines.append(f"| {feature['project']} | {feature['id']} {feature['title']} | {feature['line']} | {feature['status']} | {verdicts} |")
    lines += ["", "## 能力对照", "", "| 能力 | 项目 | 竞品 | 判定 | 证据 |", "|---|---|---|---|---|"]
    for record in ledger:
        if record.get("kind") == "capability":
            against = ", ".join(f"{c['name']}: {c['support']}" for c in record["competitors"])
            lines.append(f"| {record['feature']}: {record['about']} | {record['project']} | {against} | {record['verdict']} | `{record['test']}` |")
    text = "\n".join(lines) + "\n"
    if arguments.out:
        Path(arguments.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


# --- check (lint `lead`) ---------------------------------------------------

def problems(root: Path) -> list[tuple[str, str]]:
    """(where, what) for each violation: the features list well formed, every
    project with two to four headline features, a feature with evidence names
    a signature-scene test that exists, a claimed one has a far_ahead record."""
    features_file = root / ".agents/docs/evaluation/lead.features.omui.toml"
    metrics_file = root / "tests/bench/competitors/lead.metrics.omui.toml"
    where = features_file.relative_to(root).as_posix()
    if not features_file.exists():
        return [(where, "missing: the signature features (Part 2 plan §1.5, §4)")]
    out = []
    try:
        features = load_toml(features_file).get("feature", [])
    except tomllib.TOMLDecodeError as error:
        return [(where, f"not TOML: {error}")]
    ledger = read_jsonl(root / ".agents/docs/evaluation/lead.ledger.omui.jsonl")
    far = {r["feature"] for r in ledger if r.get("kind") in ("capability", "task") and r.get("verdict") == "far_ahead"}
    seen = set()
    headline: dict[str, int] = {}
    for feature in features:
        name = feature.get("id", "?")
        if name in seen:
            out.append((where, f"{name}: listed twice"))
        seen.add(name)
        for field in ("id", "project", "title", "line", "status"):
            if field not in feature:
                out.append((where, f"{name}: no `{field}`"))
        if feature.get("project") not in PROJECTS:
            out.append((where, f"{name}: project `{feature.get('project')}` is not one of {', '.join(PROJECTS)}"))
        if feature.get("line") not in LINES:
            out.append((where, f"{name}: line `{feature.get('line')}` is not one of {', '.join(LINES)}"))
        if feature.get("status") not in STATUSES:
            out.append((where, f"{name}: status `{feature.get('status')}` is not one of {', '.join(STATUSES)}"))
        if feature.get("line") != "foundation":
            headline[feature.get("project", "?")] = headline.get(feature.get("project", "?"), 0) + 1
        if feature.get("status") in ("evidence", "claimed"):
            scene = feature.get("scene")
            if not scene:
                out.append((where, f"{name}: status {feature['status']} needs `scene`, the signature scene's test"))
            elif not (root / scene).exists():
                out.append((where, f"{name}: scene {scene} does not exist"))
        if feature.get("status") == "claimed" and name not in far:
            out.append((where, f"{name}: claimed, but the ledger has no far_ahead capability or task record for it"))
    for project in PROJECTS:
        count = headline.get(project, 0)
        if not 2 <= count <= 4:
            out.append((where, f"{project}: {count} headline features (two to four, §1.5; the rest are foundations)"))
    if metrics_file.exists():
        try:
            metric_rows = load_toml(metrics_file).get("metric", [])
        except tomllib.TOMLDecodeError as error:
            return out + [(metrics_file.relative_to(root).as_posix(), f"not TOML: {error}")]
        ids = set()
        for metric in metric_rows:
            label = metric.get("id", "?")
            if label in ids:
                out.append((metrics_file.relative_to(root).as_posix(), f"{label}: listed twice"))
            ids.add(label)
            if metric.get("better") not in ("lower", "higher"):
                out.append((metrics_file.relative_to(root).as_posix(), f"{label}: better is lower or higher"))
            if metric.get("status", "measured") not in ("measured", "planned", "pending_hardware"):
                out.append((metrics_file.relative_to(root).as_posix(), f"{label}: status is measured, planned or pending_hardware"))
            if not metric.get("competitors"):
                out.append((metrics_file.relative_to(root).as_posix(), f"{label}: no competitors"))
    return out


def cmd_check(arguments: argparse.Namespace) -> int:
    found = problems(Path(arguments.root) if arguments.root else ROOT)
    for where, what in found:
        print(f"[lead] {where}: {what}")
    return 1 if found else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="name", required=True)
    sample = commands.add_parser("sample")
    sample.add_argument("--series", required=True)
    sample.add_argument("--round", type=int, required=True)
    sample.add_argument("--key", default="")
    sample.add_argument("--series-field", default="", help="each line's series from this field (a program printing several competitors)")
    sample.add_argument("--out", required=True)
    sample.add_argument("command", nargs=argparse.REMAINDER)
    verdict = commands.add_parser("verdict")
    verdict.add_argument("samples")
    verdict.add_argument("--metrics", default=str(METRICS))
    verdict.add_argument("--ledger", default=str(LEDGER))
    verdict.add_argument("--dry-run", action="store_true")
    capability = commands.add_parser("capability")
    capability.add_argument("--project", required=True)
    capability.add_argument("--feature", required=True)
    capability.add_argument("--about", required=True)
    capability.add_argument("--test", required=True)
    capability.add_argument("--competitor", action="append", required=True)
    capability.add_argument("--ledger", default=str(LEDGER))
    summary = commands.add_parser("summary")
    summary.add_argument("--metrics", default=str(METRICS))
    summary.add_argument("--features", default=str(FEATURES))
    summary.add_argument("--ledger", default=str(LEDGER))
    summary.add_argument("--out")
    check = commands.add_parser("check")
    check.add_argument("root", nargs="?")
    arguments = parser.parse_args()
    if arguments.name == "sample" and arguments.command and arguments.command[0] == "--":
        arguments.command = arguments.command[1:]
    return {"sample": cmd_sample, "verdict": cmd_verdict, "capability": cmd_capability, "summary": cmd_summary, "check": cmd_check}[arguments.name](arguments)


if __name__ == "__main__":
    sys.exit(main())
