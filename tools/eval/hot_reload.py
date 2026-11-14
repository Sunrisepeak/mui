#!/usr/bin/env python3
"""Save to screen (W2, hot reload ≤ 100 ms at p95): a TypeScript application
run in development mode with a real clock; its script saved again and
again, a word of its title changed each time; how long until the semantic
tree shows the new title (read through the agent protocol every few
milliseconds, so the figure includes that read).

  hot_reload.py --omui OMUI --program PROGRAM --app-dir DIR [--rounds 20]

DIR is copied; the copy's logic/main.ts is the one rewritten.
"""
from __future__ import annotations

import argparse
import json
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def title(omui: str, endpoints: str, app: str) -> str:
    done = subprocess.run([omui, "--dir", endpoints, "agent", "find", app, "--name", "Text*"], capture_output=True, text=True, timeout=10)
    for line in done.stdout.splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        for node in (record.get("data") or {}).get("nodes", []) if isinstance(record.get("data"), dict) else []:
            if node.get("role") == "text":
                return node.get("name", "")
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--omui", required=True)
    parser.add_argument("--program", required=True)
    parser.add_argument("--app-dir", required=True)
    parser.add_argument("--rounds", type=int, default=20)
    a = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix="omui-hot-"))
    for part in ("mcpp.toml", "logic"):
        source = Path(a.app_dir) / part
        (shutil.copytree if source.is_dir() else shutil.copy)(source, work / part)
    script = work / "logic" / "main.ts"
    original = script.read_text(encoding="utf-8")
    endpoints = tempfile.mkdtemp(prefix="omui-hot-ep-")
    app = subprocess.Popen([a.program, "--dev", "--serve", "--agent-dir", endpoints], cwd=work, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            listed = subprocess.run([a.omui, "--dir", endpoints, "agent", "ls"], capture_output=True, text=True).stdout.split("\t")
            if listed and listed[0]:
                name = listed[0]
                break
            time.sleep(0.1)
        else:
            sys.exit("hot_reload.py: the application did not start")
        took = []
        for round_ in range(a.rounds):
            word = f"Text statistics {round_}"
            script.write_text(original.replace("Text statistics", word), encoding="utf-8")
            start = time.perf_counter()
            while title(a.omui, endpoints, name) != word:
                if time.perf_counter() - start > 5:
                    sys.exit(f"hot_reload.py: round {round_} did not show within 5 s")
            took.append((time.perf_counter() - start) * 1000)
            time.sleep(0.2)
        took.sort()
        p95 = took[min(len(took) - 1, round(0.95 * len(took)) - 1)]
        print(json.dumps({"rounds": len(took), "median_ms": round(statistics.median(took), 1), "p95_ms": round(p95, 1), "max_ms": round(took[-1], 1)}))
    finally:
        app.terminate()
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
