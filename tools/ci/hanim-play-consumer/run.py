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
    forbidden = {"openmui.core", "openmui.sdk", "openmui.testing", "mlibs.testing", "openlifa.openmui", "openmui.spec"}
    required = {"mcpplibs.hanim", "hanim.core", "openmui.render.vector", "mlibs.raster", "mlibs.image", "mlibs.control", "openlifa.openas"}
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
    required_sources = {str(repo / "modules/gfx/hanim/src" / role / (role + ".cpp"))
                        for role in ("object", "effect", "library", "program", "engine")}
    required_sources.add(str(repo / "modules/gfx/hanim/src/render/vector/vector.cpp"))
    if not required_sources.issubset(sources):
        raise RuntimeError("actual CDB omitted a selected product implementation")
    shutil.copyfile(resolution, artifacts / f"{profile}.resolution.json")
    shutil.copyfile(cdb, artifacts / f"{profile}.compile_commands.json")
    return {"packages": sorted(names), "compiled_sources": sources,
            "source_files_sha256": {str(Path(p).relative_to(repo)): digest(Path(p)) for p in sources if Path(p).is_relative_to(repo)},
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
    report = {"format": "hanim.independent-play-consumer@0.2", "ok": False, "project": str(project),
              "source_commit": subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip(),
              "fixture_sha256": digest(fixture / "src/main.cpp"), "source_lock_before": lock,
              "runner_sha256": digest(fixture / "run.py"), "manifest_sha256": digest(fixture / "mcpp.toml.in"),
              "raster_contract": {"source": str(repo / "modules/gfx/render/vector/tests/shape_morph_test.cpp") + ":95",
                                  "source_sha256": digest(repo / "modules/gfx/render/vector/tests/shape_morph_test.cpp"),
                                  "max_boundary_coverage_u8": 18, "off_boundary_changes": 0,
                                  "coverage_total_u8_budget": 8192,
                                  "basis": "Existing 1/32-device-pixel flattening error per representation; independent exact subdivision may differ by 1/16 pixel. Boundary-only coverage tolerance; uniform interior/exterior exact."},
              "commands": [], "profiles": [], "limits": [
                  "Fixed triangle/circle/diamond fixture; not a general asset editor or complete Engine acceptance.",
                  "CPU reference pixels only; no GUI, window, physical GPU performance or realtime aesthetic claim.",
                  "Native hanim consumer uses shared authorization/context; no generic provider registry or new wire protocol.",
                  "No sanitizer claim: this gate runs dev and release."]}

    def run(label: str, argv: list[str], bootstrap: bool = False, expected: int = 0, cwd: Path | None = None) -> str:
        env = os.environ.copy()
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        env.setdefault("MCPP_JOBS", "2")
        if bootstrap:
            env.pop("MCPP_LOCKED", None)
        else:
            env["MCPP_LOCKED"] = "1"
        result = subprocess.run(argv, cwd=cwd or project, env=env, capture_output=True, text=True)
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
        for key, relative in {"engine": "gfx/hanim", "raster": "gfx/raster",
                              "image": "base/image", "hash": "base/hash", "control": "base/control"}.items():
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
            for label, time, region, retarget, bend in (
                ("start", 0, "0,0,320,240", False, 0), ("middle", .5, "0,0,320,240", False, 0),
                ("end", 1, "0,0,320,240", False, 0), ("crop", .5, "80,40,160,160", False, 0),
                ("retarget", .5, "0,0,320,240", True, 0),
                ("path-start", 0, "0,0,320,240", False, .35), ("path-middle", .5, "0,0,320,240", False, .35),
                ("path-end", 1, "0,0,320,240", False, .35), ("path-retarget", .5, "0,0,320,240", True, .35),
            ):
                png = artifacts / f"{profile}-{label}.png"
                argv = [args.mcpp, "run", "--locked", "--profile", profile, "--", "--time", str(time),
                        "--region", region, "--bend", str(bend), "--out", str(png)] + (["--retarget"] if retarget else [])
                text = run(profile + "-" + label, argv)
                rows = [json.loads(line) for line in text.splitlines() if line.lstrip().startswith("{")]
                rows = [row for row in rows if row.get("kind") == "hanim.play-consumer"]
                if len(rows) != 1 or not rows[0].get("ok"):
                    raise RuntimeError(f"{profile}-{label}: no successful provenance")
                result = rows[0]
                if result["vertex_path"] != {"stage": "after-correspondence", "space": "endpoint-chord", "bend": bend}:
                    raise RuntimeError("process path provenance disagrees")
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
            if (results[0]["pixel_fnv1a"] != results[5]["pixel_fnv1a"] or
                    results[2]["pixel_fnv1a"] != results[7]["pixel_fnv1a"] or
                    results[1]["pixel_fnv1a"] == results[6]["pixel_fnv1a"]):
                raise RuntimeError("process path must preserve endpoint pixels and change actual middle pixels")
            for result in (results[4], results[8]):
                if result["target"] == result["initial_target"] or result["controls"]["retarget_pixel_max_u8"] > 18 or result["controls"]["retarget_coverage_off_boundary"] or result["controls"]["retarget_styled_off_boundary"]:
                    raise RuntimeError("real retarget target/continuity failed")
            agent_results = []
            for label, bend in (("agent-linear", 0), ("agent-curved", .35)):
                png = artifacts / f"{profile}-{label}.png"
                text = run(profile + "-" + label, [args.mcpp, "run", "--locked", "--profile", profile, "--",
                           "--out", str(png), "--bend", str(bend), "--agent-review"])
                rows = [json.loads(line) for line in text.splitlines() if line.lstrip().startswith("{")]
                result = next((row for row in rows if row.get("kind") == "hanim.play-consumer"), None)
                if not result or not result.get("ok"):
                    raise RuntimeError("independent Agent review did not finish")
                review = result["agent_review"]
                if review["authorization"] != "mlibs.control/openas" or review["reverse_replayed"] != 61:
                    raise RuntimeError("shared authority or process replay missing")
                for key in ("read_only_preserved_state", "dry_run_preserved_state", "foreign_session_refused",
                            "revoked_continuation_refused", "claimed_provenance_replaced", "nested_operation_filter_preserved"):
                    if review.get(key) is not True:
                        raise RuntimeError(f"Agent control contract failed: {key}")
                frames = review["frames"]
                if len(frames) != 61 or len({frame["pixel_fnv1a"] for frame in frames}) != 61:
                    raise RuntimeError("expected 61 distinct actual process frames")
                for index, frame in enumerate(frames):
                    if (frame["index"], frame["time_numerator"], frame["time_denominator"]) != (index, index, 60):
                        raise RuntimeError("process frame time provenance disagrees")
                    if frame["sample_time_seconds"] != index / 60:
                        raise RuntimeError("rendered sample time differs from requested rational time")
                    image = Path(frame["png"])
                    raw = image.read_bytes()
                    if raw[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", raw[16:24]) != (320, 240):
                        raise RuntimeError("process frame is not the actual viewport PNG")
                    frame["png_sha256"] = digest(image)
                if digest(Path(frames[30]["png"])) != digest(png):
                    raise RuntimeError("Agent midpoint differs from the same native renderer output")
                agent_results.append(result)
            denied = artifacts / f"{profile}-agent-denied.png"
            denied_text = run(profile + "-agent-denied", [args.mcpp, "run", "--locked", "--profile", profile, "--",
                              "--out", str(denied), "--agent-review", "--agent-deny-capture"], expected=3)
            denials = [json.loads(line) for line in denied_text.splitlines() if line.lstrip().startswith("{")]
            if not any(row.get("error") == "Agent capture refused" and row.get("ok") is False for row in denials):
                raise RuntimeError("capture denial did not return its structured failure")
            if list(artifacts.glob(denied.name + "*")):
                raise RuntimeError("a refused visual query published pixels or provenance")
            invalid = artifacts / f"{profile}-invalid.png"
            run(profile + "-invalid-region", [args.mcpp, "run", "--locked", "--profile", profile, "--", "--out", str(invalid), "--region", "319,0,2,240"], expected=2)
            if invalid.exists():
                raise RuntimeError("invalid crop published an image")
            run(profile + "-metadata", [args.mcpp, "build", "--locked", "--profile", profile, "--configure-only"])
            report["profiles"].append({"profile": profile, "runs": results, "agent_reviews": agent_results,
                                      "denied_capture_published_no_files": True, "display_environment_removed": True,
                                      **audit(project, repo, artifacts, profile)})
            if digest(project / "mcpp.lock") != temporary_lock:
                raise RuntimeError("locked runs changed the temporary project's resolution")
        # Test-only dependencies live in a second real consumer, never in the
        # production program's resolution or compilation database.
        integration = project / "integration"
        (integration / "src").mkdir(parents=True)
        (integration / "mcpp.toml").write_text(manifest + '\n[dev-dependencies]\nmlibs.testing = { path = ' +
                                              json.dumps(str(repo / "modules/base/testing")) + ' }\n')
        shutil.copyfile(fixture / "src/main.cpp", integration / "src/main.cpp")
        shutil.copytree(fixture / "tests", integration / "tests")
        for profile in ("dev", "release"):
            run(profile + "-integration-tests", [args.mcpp, "test", "--profile", profile],
                bootstrap=profile == "dev", cwd=integration)
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
