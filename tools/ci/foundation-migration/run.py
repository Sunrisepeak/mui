#!/usr/bin/env python3
"""Check real source and packed foundation consumers with new/old/mixed names."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import time

NAMES = ("hash", "math", "log", "trace", "testing")


def sha(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcpp", default=os.environ.get("MCPP", "mcpp"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--keep-project", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    fixture = Path(__file__).resolve().parent
    output = (args.output or repo / "target/foundation-migration/report.omui.json").resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    artifacts = output.parent / "artifacts"
    artifacts.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="mlibs-namespace-consumer-"))
    before = sha(repo / "mcpp.lock")
    report: dict = {
        "format": "mlibs.namespace-consumer@0.1", "ok": False, "commands": [], "packs": [], "runs": [],
        "source_lock_sha256_before": before,
        "limits": ["Source compatibility after rebuild; not C++ binary ABI compatibility or registry aliases.",
                   "Native release source and archive consumers; this gate does not test other targets.",
                   "Fresh consumer build times can reuse global toolchain caches; they are not clean-machine benchmarks.",
                   "The packed testing interface imports hash; the external consumer explicitly declares both archives.",
                   "No package publication or claim that mcpp pack preserves dependency metadata."],
    }

    def command(label: str, argv: list[str], cwd: Path, *, bootstrap: bool = False) -> str:
        env = os.environ.copy()
        env.setdefault("MCPP_JOBS", "2")
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        if bootstrap:
            env.pop("MCPP_LOCKED", None)
        else:
            env["MCPP_LOCKED"] = "1"
        started = time.monotonic()
        result = subprocess.run(argv, cwd=cwd, env=env, text=True, capture_output=True, timeout=600)
        log = artifacts / (label + ".log")
        log.write_text(result.stdout + "\nSTDERR\n" + result.stderr, encoding="utf-8")
        report["commands"].append({"label": label, "argv": argv, "cwd": str(cwd),
                                   "exit_code": result.returncode, "seconds": time.monotonic() - started, "log": str(log)})
        if result.returncode:
            raise RuntimeError(f"{label}: exit {result.returncode}; {log}")
        return result.stdout

    try:
        source_paths = {name: repo / "modules/base" / name for name in NAMES}
        packed_paths = {}
        for name, path in source_paths.items():
            stdout = command("pack-" + name, [args.mcpp, "pack", "-p", "mlibs." + name,
                             "--profile", "release", "--message-format", "json"], repo)
            decoder = json.JSONDecoder()
            envelopes = []
            for index, char in enumerate(stdout):
                if char != "{":
                    continue
                try:
                    value, _ = decoder.raw_decode(stdout[index:])
                    if isinstance(value, dict) and value.get("kind") == "mcpp.pack":
                        envelopes.append(value)
                except ValueError:
                    pass
            if len(envelopes) != 1:
                raise RuntimeError("missing unique mcpp.pack envelope")
            archive = Path(envelopes[0]["data"]["artifacts"][0]["path"])
            unpack = work / "packed" / name
            unpack.mkdir(parents=True)
            with tarfile.open(archive) as tar:
                tar.extractall(unpack, filter="data")
            manifests = list(unpack.glob("*/mcpp.toml"))
            if len(manifests) != 1:
                raise RuntimeError("unexpected packed layout")
            packed = manifests[0].parent
            units = list((packed / "interface").glob("*.cppm"))
            text = "\n".join(unit.read_text() for unit in units)
            for module in ("mlibs." + name, "openmui." + name):
                if "export module " + module + ";" not in text:
                    raise RuntimeError(f"packed public interface is missing {module}")
            if list(packed.rglob("*.cpp")):
                raise RuntimeError("packed consumer must link compiled implementations")
            packed_paths[name] = packed
            shutil.copyfile(manifests[0], artifacts / (name + ".packed.mcpp.toml"))
            report["packs"].append({"name": "mlibs." + name, "archive": str(archive), "sha256": sha(archive),
                                    "archive_bytes": archive.stat().st_size,
                                    "library_bytes": sum(p.stat().st_size for p in packed.rglob("*.a")),
                                    "interfaces": sorted(p.name for p in units)})

        for source_kind, paths in (("source", source_paths), ("packed", packed_paths)):
            for style in ("canonical", "legacy", "mixed"):
                label = source_kind + "-" + style
                project = work / label
                (project / "src").mkdir(parents=True)
                prefixes = ("mlibs", "openmui") if style == "mixed" else (("mlibs",) if style == "canonical" else ("openmui",))
                dependencies = "\n".join(f'{prefix}.{name} = {{ path = {json.dumps(str(paths[name]))} }}'
                                         for prefix in prefixes for name in NAMES)
                imports = "\n".join(f"import {prefix}.{name};" for prefix in prefixes for name in NAMES)
                (project / "mcpp.toml").write_text((fixture / "mcpp.toml.in").read_text().replace("{{dependencies}}", dependencies))
                (project / "src/main.cpp").write_text((fixture / "main.cpp.in").read_text().replace("{{imports}}", imports))
                argv = [args.mcpp, "run", "--profile", "release"]
                stdout = command(label, argv, project, bootstrap=True)
                result = [json.loads(line) for line in stdout.splitlines() if line.startswith('{"kind":"mlibs.namespace-consumer"')]
                if len(result) != 1 or not result[0].get("ok") or result[0].get("cases") != 4:
                    raise RuntimeError(label + ": missing real successful consumer result")
                own_lock = sha(project / "mcpp.lock")
                command(label + "-incremental", argv + ["--locked"], project)
                if sha(project / "mcpp.lock") != own_lock:
                    raise RuntimeError("consumer lock changed on incremental run")
                command(label + "-metadata", [args.mcpp, "build", "--profile", "release", "--locked", "--configure-only"], project)
                resolution = max((project / "target").glob("*/*/resolution.json"), key=lambda p: p.stat().st_mtime_ns)
                graph = json.loads(resolution.read_text())
                if graph.get("schema_version") != 2:
                    raise RuntimeError("unsupported mcpp resolution schema")
                packages = graph["graph"]["packages"]
                identities = []
                for package in packages:
                    data = package["package"]
                    identity = (data.get("namespace", "") + "." + data["name"]).lstrip(".")
                    identities.append(identity)
                    expected_path = project if identity == "mlibs.validation.namespace_consumer" else paths.get(data["name"])
                    if expected_path is None or data.get("source") != "path+" + str(expected_path):
                        raise RuntimeError(label + ": unexpected dependency source " + str(data))
                expected = {"mlibs.validation.namespace_consumer", *("mlibs." + name for name in NAMES)}
                if set(identities) != expected or len(identities) != len(expected):
                    raise RuntimeError(label + ": duplicate or unexpected package identities")
                cdb = json.loads((resolution.parent / "compile_commands.json").read_text())
                sources = []
                for entry in cdb:
                    directory = Path(entry["directory"])
                    if not directory.is_absolute():
                        directory = project / directory
                    path = Path(entry["file"])
                    path = (directory / path).resolve() if not path.is_absolute() else path.resolve()
                    if path == (project / "src/main.cpp") or any(path.is_relative_to(root) for root in paths.values()):
                        sources.append(str(path))
                    elif path.name in {"std.cc", "std.cppm"} and path.is_relative_to(Path(entry["arguments"][0]).resolve().parent.parent):
                        continue  # the selected compiler's standard-library module
                    else:
                        raise RuntimeError(label + ": unexpected compiled source " + str(path))
                if len(sources) != len(set(sources)):
                    raise RuntimeError(label + ": duplicated implementation compilation")
                implementations = [p for p in sources if p.endswith(".cpp") and p != str(project / "src/main.cpp")]
                if source_kind == "packed" and implementations:
                    raise RuntimeError(label + ": binary consumer silently built source implementations")
                if source_kind == "source" and len(implementations) != 5:
                    raise RuntimeError(label + ": expected one implementation unit per library")
                shutil.copyfile(resolution, artifacts / (label + ".resolution.json"))
                shutil.copyfile(project / "mcpp.toml", artifacts / (label + ".mcpp.toml"))
                report["runs"].append({"kind": source_kind, "style": style, "result": result[0],
                                       "packages": sorted(identities), "compiled_sources": sources,
                                       "toolchain": graph["toolchain"]})
        report["ok"] = True
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
    finally:
        report["source_lock_sha256_after"] = sha(repo / "mcpp.lock")
        if before != report["source_lock_sha256_after"]:
            report["ok"] = False
            report["error"] = "repository lock changed"
        output.write_text(json.dumps(report, indent=2) + "\n")
        if not args.keep_project:
            shutil.rmtree(work)
    print(json.dumps({"ok": report["ok"], "runs": len(report["runs"]), "report": str(output),
                      **({"error": report["error"]} if "error" in report else {})}))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
