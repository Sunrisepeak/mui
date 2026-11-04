#!/usr/bin/env python3
"""Build and run a fresh windowless public Engine/vector consumer, then audit it."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def digest(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None


def audit(project: Path, repo: Path, artifacts: Path, profile: str) -> dict:
    resolutions = list((project / "target").glob("*/*/resolution.json"))
    if not resolutions:
        raise RuntimeError("mcpp produced no actual resolution metadata")
    resolution = max(resolutions, key=lambda path: path.stat().st_mtime_ns)
    data = json.loads(resolution.read_text())
    if data.get("schema_version") != 2:
        raise RuntimeError("unrecognized resolution schema")
    names = []
    forbidden = {"openmui.core", "openmui.sdk", "openmui.testing", "openlifa.openmui", "openmui.spec"}
    required = {"mcpplibs.hanim", "hanim.engine", "hanim.program", "hanim.library", "hanim.object", "hanim.core", "hanim.effect",
                "hanim.render.vector", "openmui.render.vector", "openmui.raster", "openmui.image"}
    def independent_source(path: Path) -> None:
        path = path.resolve()
        if path.is_relative_to(repo):
            relative = path.relative_to(repo)
            if len(relative.parts) < 2 or relative.parts[:2] not in {
                ("modules", "base"), ("modules", "gfx"), ("modules", "content"), ("modules", "media")
            }:
                raise RuntimeError(f"non-independent repository source in closure: {path}")
    for package in data["graph"]["packages"]:
        item = package["package"]
        name = (item.get("namespace", "") + "." + item["name"]).lstrip(".")
        names.append(name)
        if name in forbidden or name.startswith(("openmui.ui.", "openmui.host.", "openmui.site")):
            raise RuntimeError(f"forbidden UI/spec/testing/host package: {name}")
        source = item.get("source", "")
        if source.startswith("path+"):
            independent_source(Path(source[5:]))
    if not required.issubset(names):
        raise RuntimeError(f"missing real public consumers: {sorted(required-set(names))}")
    cdb = resolution.parent / "compile_commands.json"
    entries = json.loads(cdb.read_text())
    sources = []
    for entry in entries:
        source = Path(entry["file"])
        if not source.is_absolute():
            directory = Path(entry["directory"])
            source = (directory if directory.is_absolute() else project / directory) / source
        independent_source(source)
        sources.append(str(source.resolve()))
    if str(project / "src/main.cpp") not in sources:
        raise RuntimeError("actual CDB omitted consumer")
    shutil.copyfile(resolution, artifacts / f"{profile}.resolution.json")
    shutil.copyfile(cdb, artifacts / f"{profile}.compile_commands.json")
    return {"packages": sorted(names), "compiled_sources": sources,
            "resolution": str(artifacts / f"{profile}.resolution.json"),
            "cdb": str(artifacts / f"{profile}.compile_commands.json"), "toolchain": data["toolchain"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--mcpp", default=os.environ.get("MCPP", "mcpp"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--keep-project", action="store_true")
    args = parser.parse_args()
    repo, output = args.source_root.resolve(), args.output.resolve()
    fixture = Path(__file__).resolve().parent
    output.parent.mkdir(parents=True, exist_ok=True)
    artifacts = output.parent / (output.stem + "-artifacts")
    artifacts.mkdir(exist_ok=True)
    project = Path(tempfile.mkdtemp(prefix="hanim-public-play-consumer-"))
    lock = digest(repo / "mcpp.lock")
    report = {"format": "hanim.independent-play-consumer@0.1", "ok": False, "project": str(project),
              "fixture_sha256": digest(fixture / "src/main.cpp"), "source_lock_before": lock,
              "raster_contract": {"source": str(repo / "modules/gfx/render/vector/tests/shape_morph_test.cpp") + ":95",
                                  "source_sha256": digest(repo / "modules/gfx/render/vector/tests/shape_morph_test.cpp"),
                                  "max_boundary_coverage_u8": 18, "off_boundary_changes": 0,
                                  "coverage_total_u8_budget": 8192,
                                  "basis": "Existing 1/32-device-pixel flattening error per representation; independent exact subdivision may differ by 1/16 pixel. Boundary-only coverage tolerance; uniform interior/exterior exact."},
              "commands": [], "profiles": [], "limits": [
                  "Fixed triangle/circle/diamond fixture; not a general asset editor or complete Engine acceptance.",
                  "CPU reference pixels only; no GUI, window, physical GPU performance or Agent provider claim.",
                  "No sanitizer claim: this gate runs dev and release."]}

    def run(label: str, argv: list[str], bootstrap: bool = False, expected: int = 0) -> str:
        env = os.environ.copy()
        env.setdefault("MCPP_JOBS", "2")
        if bootstrap:
            env.pop("MCPP_LOCKED", None)
        else:
            env["MCPP_LOCKED"] = "1"
        result = subprocess.run(argv, cwd=project, env=env, capture_output=True, text=True)
        stdout, stderr = artifacts / f"{label}.stdout.log", artifacts / f"{label}.stderr.log"
        stdout.write_text(result.stdout)
        stderr.write_text(result.stderr)
        report["commands"].append({"argv": argv, "exit_code": result.returncode, "stdout": str(stdout), "stderr": str(stderr)})
        if result.returncode != expected:
            raise RuntimeError(f"{label}: exit {result.returncode}, expected {expected}; see {stderr}")
        return result.stdout

    try:
        (project / "src").mkdir()
        manifest = (fixture / "mcpp.toml.in").read_text()
        for key, relative in {"engine": "gfx/hanim", "adapter": "gfx/hanim/render/vector", "raster": "gfx/raster",
                              "image": "base/image", "hash": "base/hash"}.items():
            manifest = manifest.replace("{{" + key + "_path}}", json.dumps(str(repo / "modules" / relative)))
        (project / "mcpp.toml").write_text(manifest)
        shutil.copyfile(fixture / "src/main.cpp", project / "src/main.cpp")
        shutil.copyfile(project / "mcpp.toml", artifacts / "consumer.mcpp.toml")
        shutil.copyfile(project / "src/main.cpp", artifacts / "consumer.cpp")
        run("bootstrap", [args.mcpp, "build", "--profile", "dev"], bootstrap=True)
        temporary_lock = digest(project / "mcpp.lock")
        report["temporary_lock_sha256"] = temporary_lock
        for profile in ("dev", "release"):
            results = []
            for label, time, region, retarget in (
                ("start", 0, "0,0,320,240", False), ("middle", .5, "0,0,320,240", False),
                ("end", 1, "0,0,320,240", False), ("crop", .5, "80,40,160,160", False),
                ("retarget", .5, "0,0,320,240", True),
            ):
                png = artifacts / f"{profile}-{label}.png"
                argv = [args.mcpp, "run", "--locked", "--profile", profile, "--", "--time", str(time),
                        "--region", region, "--out", str(png)] + (["--retarget"] if retarget else [])
                text = run(profile + "-" + label, argv)
                rows = [json.loads(line) for line in text.splitlines() if line.lstrip().startswith("{")]
                rows = [row for row in rows if row.get("kind") == "hanim.play-consumer"]
                if len(rows) != 1 or not rows[0].get("ok"):
                    raise RuntimeError(f"{profile}-{label}: no successful provenance")
                result = rows[0]
                if result["sample_time_seconds"] != time or result["parts"] != 1 or result["contours"] != 1 or result["visible_pixels"] <= 100:
                    raise RuntimeError("consumer time/geometry/visible pixels disagree")
                if result["backend"] != "cpu-reference" or result["controls"]["retarget"] != retarget or not result["controls"]["pause_holds"] or not result["controls"]["reverse_moves_backwards"]:
                    raise RuntimeError("consumer controls/backend disagree")
                raw = png.read_bytes()
                size = struct.unpack(">II", raw[16:24]) if raw[:8] == b"\x89PNG\r\n\x1a\n" else None
                if size != (result["region"]["width"], result["region"]["height"]):
                    raise RuntimeError("actual PNG IHDR disagrees with region provenance")
                if json.loads(Path(str(png) + ".provenance.omui.json").read_text()) != result:
                    raise RuntimeError("sidecar provenance differs from stdout")
                results.append(result)
            if len({row["pixel_fnv1a"] for row in results[:3]}) != 3:
                raise RuntimeError("actual endpoint/midpoint pixels must differ")
            if results[-1]["target"] == results[-1]["initial_target"] or results[-1]["controls"]["retarget_pixel_max_u8"] > 18 or results[-1]["controls"]["retarget_coverage_off_boundary"] or results[-1]["controls"]["retarget_styled_off_boundary"]:
                raise RuntimeError("real retarget target/continuity failed")
            invalid = artifacts / f"{profile}-invalid.png"
            run(profile + "-invalid-region", [args.mcpp, "run", "--locked", "--profile", profile, "--", "--out", str(invalid), "--region", "319,0,2,240"], expected=2)
            if invalid.exists():
                raise RuntimeError("invalid crop published an image")
            run(profile + "-metadata", [args.mcpp, "build", "--locked", "--profile", profile, "--configure-only"])
            report["profiles"].append({"profile": profile, "runs": results, **audit(project, repo, artifacts, profile)})
            if digest(project / "mcpp.lock") != temporary_lock:
                raise RuntimeError("locked runs changed the temporary project's resolution")
        report["ok"] = True
    except (OSError, RuntimeError, KeyError, ValueError) as error:
        report["error"] = str(error)
    finally:
        report["source_lock_after"] = digest(repo / "mcpp.lock")
        if report["source_lock_after"] != lock:
            report["ok"] = False
            report["error"] = "source repository lock changed"
        report["project_retained"] = args.keep_project
        output.write_text(json.dumps(report, indent=2) + "\n")
        if not args.keep_project:
            shutil.rmtree(project)
    print(json.dumps({"ok": report["ok"], "report": str(output), "profiles": len(report["profiles"]),
                      **({"error": report["error"]} if "error" in report else {})}))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
