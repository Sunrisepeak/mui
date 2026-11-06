#!/usr/bin/env python3
"""Build a fresh public core/log consumer and audit mcpp's resolved graph."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def digest(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None


def inside(path: Path, parent: Path) -> bool:
    return path == parent or parent in path.parents


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcpp", default=os.environ.get("MCPP", "mcpp"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--keep-project", action="store_true", help="retain the fresh /tmp project for inspection")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    fixture = repo / "tools/ci/hanim-core-consumer"
    output = (args.output or repo / "target/hanim-core-consumer/report.omui.json").resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    artifacts = output.parent / (output.stem + "-artifacts")
    artifacts.mkdir(exist_ok=True)
    project = Path(tempfile.mkdtemp(prefix="hanim-public-core-consumer-"))
    lock_before = digest(repo / "mcpp.lock")
    report: dict = {
        "format": "hanim.independent-core-consumer@0.1", "ok": False,
        "project": str(project), "commands": [], "runs": [],
        "fixture_sha256": digest(fixture / "src/main.cpp"),
        "source_lock_sha256_before": lock_before,
        "limits": ["Public core geometry/timeline/log evidence only; not HObject/Engine/A1 or pixel-domain acceptance.",
                   "No renderer, UI, spec, testing, site, host or GPU is consumed.",
                   "No new performance, allocation or physical-GPU claim."],
    }

    def command(label: str, argv: list[str], *, bootstrap: bool = False) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env.setdefault("MCPP_JOBS", "2")
        if bootstrap:
            env.pop("MCPP_LOCKED", None)  # only this freshly generated project's own lock
        else:
            env["MCPP_LOCKED"] = "1"
        result = subprocess.run(argv, cwd=project, env=env, text=True, capture_output=True)
        stdout = artifacts / f"{label}.stdout.log"
        stderr = artifacts / f"{label}.stderr.log"
        stdout.write_text(result.stdout, encoding="utf-8")
        stderr.write_text(result.stderr, encoding="utf-8")
        report["commands"].append({"argv": argv, "cwd": str(project), "exit_code": result.returncode,
                                   "stdout": str(stdout), "stderr": str(stderr)})
        if result.returncode:
            raise RuntimeError(f"{label} exited {result.returncode}; see {stderr}")
        return result

    try:
        (project / "src").mkdir()
        manifest = (fixture / "mcpp.toml.in").read_text(encoding="utf-8")
        for key, path in {"core_path": repo / "modules/projects/hanim/core", "utils_path": repo / "modules/base/utils"}.items():
            manifest = manifest.replace("{{" + key + "}}", json.dumps(path.as_posix(), ensure_ascii=False))
        (project / "mcpp.toml").write_text(manifest, encoding="utf-8")
        shutil.copyfile(fixture / "src/main.cpp", project / "src/main.cpp")
        shutil.copyfile(project / "mcpp.toml", artifacts / "consumer.mcpp.toml")
        shutil.copyfile(project / "src/main.cpp", artifacts / "consumer.cpp")
        # There is no inherited repository lock/workspace. Resolve this local
        # project's own path-only graph once, then require it unchanged.
        command("bootstrap", [args.mcpp, "build", "--profile", "dev"], bootstrap=True)
        temporary_lock = digest(project / "mcpp.lock")
        report["temporary_lock_sha256"] = temporary_lock
        # mcpp 2026.11.2.2 emits no lock for a path-only graph: there
        # are no index resolutions to pin. --locked still validates every
        # subsequent invocation; an absent lock must remain absent.
        report["temporary_lock_required"] = temporary_lock is not None
        expected_paths = {"hanim.validation.core_consumer": project, "hanim.core": repo / "modules/projects/hanim/core",
                          "mlibs.math": repo / "modules/base/math", "mlibs.utils": repo / "modules/base/utils"}
        expected = set(expected_paths)
        for profile in ("dev", "release"):
            run = command(profile, [args.mcpp, "run", "--locked", "--profile", profile])
            rows = [json.loads(line) for line in run.stdout.splitlines() if line.lstrip().startswith("{")]
            results = [row for row in rows if row.get("kind") == "hanim.core-consumer"]
            logs = [row for row in rows if "level" in row and "category" in row]
            if len(results) != 1 or not results[0].get("ok"):
                raise RuntimeError(f"{profile}: missing successful real consumer result")
            result = results[0]
            for field, value in {"source_lines": 3, "target_cubics": 4, "contours": 1, "seek_count": 6,
                                 "timeline_channels": 2, "log_records": 8, "repeated_seek_equal": True,
                                 "endpoints_differ": True}.items():
                if result.get(field) != value:
                    raise RuntimeError(f"{profile}: unexpected {field}: {result.get(field)!r}")
            if not 4 <= result.get("prepared_cubics", 0) <= 64:
                raise RuntimeError(f"{profile}: preparation segment budget not respected")
            if len(logs) != 8 or any(row["category"] != "hanim.consumer" or row["level"] != "info" for row in logs):
                raise RuntimeError(f"{profile}: real sink/history records differ")
            if any(not row.get("line") or not row.get("time") or "main.cpp" not in row.get("file", "") for row in logs):
                raise RuntimeError(f"{profile}: missing caller/history metadata")
            # This supported CLI emits the actual configured build graph/CDB.
            # Audit it after the successful run, never a manifest-only guess.
            command(profile + "-metadata", [args.mcpp, "build", "--locked", "--profile", profile, "--configure-only"])
            candidates = list((project / "target").glob("*/*/resolution.json"))
            if not candidates:
                raise RuntimeError(f"{profile}: mcpp did not emit resolution.json")
            resolution = max(candidates, key=lambda path: path.stat().st_mtime_ns)
            metadata = json.loads(resolution.read_text(encoding="utf-8"))
            if metadata.get("schema_version") != 2:
                raise RuntimeError("unrecognized mcpp resolution schema; review graph audit")
            packages = metadata["graph"]["packages"]
            identities = set()
            for package in packages:
                data = package["package"]
                name = (data.get("namespace", "") + "." + data["name"]).lstrip(".")
                identities.add(name)
                if name not in expected_paths or data.get("source") != "path+" + expected_paths[name].as_posix():
                    raise RuntimeError(f"{profile}: unexpected actual package/source {name}: {data.get('source')}")
            if identities != expected:
                raise RuntimeError(f"{profile}: unexpected actual closure {sorted(identities)}")
            cdb = resolution.parent / "compile_commands.json"
            entries = json.loads(cdb.read_text(encoding="utf-8"))
            allowed_sources = [project, repo / "modules/projects/hanim/core", repo / "modules/base/math", repo / "modules/base/utils"]
            sources = []
            for entry in entries:
                source = Path(entry["file"])
                if not source.is_absolute():
                    directory = Path(entry["directory"])
                    if not directory.is_absolute():
                        directory = project / directory
                    source = directory / source
                source = source.resolve()
                sources.append(str(source))
                if inside(source, repo) and not any(inside(source, allowed) for allowed in allowed_sources):
                    raise RuntimeError(f"{profile}: unexpected repository source in actual CDB: {source}")
            if str((project / "src/main.cpp").resolve()) not in sources:
                raise RuntimeError(f"{profile}: consumer missing from actual CDB")
            snapshot = artifacts / f"{profile}.resolution.json"
            shutil.copyfile(resolution, snapshot)
            report["runs"].append({"profile": profile, "result": result, "log_records": logs,
                                   "actual_packages": packages, "actual_package_names": sorted(identities),
                                   "metadata_command": report["commands"][-1]["argv"], "resolution": str(snapshot),
                                   "toolchain": metadata["toolchain"], "compiled_sources": sources})
            if digest(project / "mcpp.lock") != temporary_lock:
                raise RuntimeError("a locked run changed the temporary resolution")
        report["ok"] = True
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        report["error"] = str(error)
    finally:
        report["source_lock_sha256_after"] = digest(repo / "mcpp.lock")
        if report["source_lock_sha256_after"] != lock_before:
            report["ok"] = False
            report["error"] = "repository lock changed during standalone consumption"
        report["project_retained"] = args.keep_project
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if not args.keep_project:
            shutil.rmtree(project)
    print(json.dumps({"ok": report["ok"], "report": str(output), "profiles": len(report["runs"]),
                      **({"error": report["error"]} if "error" in report else {})}))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
