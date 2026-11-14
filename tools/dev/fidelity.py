#!/usr/bin/env python3
"""The reference beside another renderer, scenario by scenario: model-viewer's render-fidelity scenarios
(Khronos' sample models, each with its camera and its HDR surroundings) drawn by `xrender look` and measured
against the suite's golden pictures in the suite's own measure.

    tools/dev/fidelity.py --xrender PATH [--cache DIR] [--follow RAYS] [--out DIR] [NAME …]

Names are the scenarios' (`khronos-Duck`), or the models' (`Duck`); none: every scenario whose model is one
of Khronos' samples (the file the scenario names: a .glb, or a .gltf with the files it names beside it) and
whose surroundings are an HDR picture. What it reads is fetched once into the
cache (default ~/.cache/omui/fidelity): the suite's list, each scenario's golden and surroundings from
google/model-viewer, each model from KhronosGroup/glTF-Sample-Assets. Those files are other people's: nothing
of them is run, and nothing of them is put in this repository. Prints a Markdown table; exit 1 if a scenario
that was asked for could not be drawn.
"""
import argparse
import json
import pathlib
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

SUITE = "https://raw.githubusercontent.com/google/model-viewer/master/packages"
SAMPLES = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models"
# What a scenario takes where it says nothing (render-fidelity-tools' config-reader.ts).
DEFAULTS = {"lighting": "../../../shared-assets/environments/lightroom_14b.hdr", "dimensions": {"width": 768, "height": 768},
            "target": {"x": 0, "y": 0, "z": 0}, "orbit": {"theta": 0, "phi": 90, "radius": 1}, "verticalFoV": 45}
PIXEL_RATIO = 2  # the goldens are made at two device pixels a pixel


def fetched(url: str, to: pathlib.Path) -> bool:
    if to.exists() and to.stat().st_size > 0:
        return True
    to.parent.mkdir(parents=True, exist_ok=True)
    try:
        with urllib.request.urlopen(url, timeout=120) as answer:
            data = answer.read()
    except (urllib.error.URLError, TimeoutError):
        return False
    to.write_bytes(data)
    return True


def model_fetched(said: str, cache: pathlib.Path) -> tuple[pathlib.Path | None, str]:
    """The scenario's model in the cache — the file it names under the samples' Models/, and for a .gltf every
    file that names by a relative path (buffers, pictures) beside it. (None, why) where it could not be had."""
    parts = pathlib.PurePosixPath(said).parts
    under = parts[parts.index("Models") + 1:]  # Name/glTF-Binary/Name.glb, Name/glTF/Name.gltf, …
    if len(under) != 3 or any(part in ("", ".", "..") for part in under):
        return None, "its model is not where the samples keep one"
    model = cache / "models" / pathlib.Path(*under)
    if not fetched(f"{SAMPLES}/{'/'.join(under)}", model):
        return None, "its model could not be fetched"
    if model.suffix != ".gltf":
        return model, ""
    try:
        document = json.loads(model.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None, "its model is not a glTF document"
    for named in list(document.get("buffers", [])) + list(document.get("images", [])):
        uri = named.get("uri") if isinstance(named, dict) else None
        if not isinstance(uri, str) or uri.startswith("data:"):
            continue
        beside = pathlib.PurePosixPath(urllib.parse.unquote(uri))
        # Another's file names these: only a plain path below the model's own directory is fetched.
        if beside.is_absolute() or ":" in uri or any(part in ("", ".", "..") for part in beside.parts):
            return None, f"its model names a file outside its directory ({uri})"
        if not fetched(f"{SAMPLES}/{under[0]}/{under[1]}/{urllib.parse.quote(str(beside))}", model.parent / pathlib.Path(*beside.parts)):
            return None, f"a file its model names could not be fetched ({uri})"
    return model, ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--xrender", required=True, help="the built xrender command")
    parser.add_argument("--cache", default=str(pathlib.Path.home() / ".cache" / "omui" / "fidelity"))
    parser.add_argument("--out", default="", help="where the pictures drawn go (default: the cache's `drawn`)")
    parser.add_argument("--follow", type=int, default=0, help="rays a pixel for the light followed way by way (0: not followed)")
    parser.add_argument("names", nargs="*")
    args = parser.parse_args()
    cache = pathlib.Path(args.cache)
    out = pathlib.Path(args.out) if args.out else cache / "drawn"
    out.mkdir(parents=True, exist_ok=True)
    listing = cache / "config.json"
    if not fetched(f"{SUITE}/render-fidelity-tools/test/config.json", listing):
        print("the suite's list of scenarios could not be fetched", file=sys.stderr)
        return 1
    scenarios = json.loads(listing.read_text(encoding="utf-8"))["scenarios"]
    asked = {name.removeprefix("khronos-") for name in args.names}
    rows, failed = [], 0
    for scenario in scenarios:
        name = scenario["name"]
        short = name.removeprefix("khronos-")
        if asked and short not in asked:
            continue
        said = {**DEFAULTS, **scenario}
        for part in ("dimensions", "target", "orbit"):
            said[part] = {**DEFAULTS[part], **scenario.get(part, {})}
        skipped = None
        model = None
        if not said["lighting"].endswith(".hdr"):
            skipped = "its surroundings are not an HDR picture"
        elif "glTF-Sample-Assets/Models/" not in said["model"]:
            skipped = "its model is not one of Khronos' samples"
        elif said["orbit"]["radius"] <= 0:
            skipped = "its eye is at its target"
        if not skipped:
            model, skipped = model_fetched(said["model"], cache)
        light = cache / "environments" / pathlib.PurePosixPath(said["lighting"]).name
        if not skipped and not fetched(f"{SUITE}/shared-assets/environments/{light.name}", light):
            skipped = "its surroundings could not be fetched"
        golden = cache / "goldens" / f"{name}.png"
        if not skipped and not fetched(f"{SUITE}/render-fidelity-tools/test/goldens/{name}/model-viewer-golden.png", golden):
            skipped = "its golden could not be fetched"
        if skipped:
            if asked:
                print(f"{name}: {skipped}", file=sys.stderr)
                failed += 1
            rows.append((name, None, skipped))
            continue
        width, height = said["dimensions"]["width"] * PIXEL_RATIO, said["dimensions"]["height"] * PIXEL_RATIO
        if args.follow:
            width, height = width - width % 4, height - height % 4
        command = [args.xrender, "look", str(model), "--out", str(out / f"{name}.png"), "--light", str(light), "--golden", str(golden), "--size", f"{width}x{height}",
                   "--orbit", f"{said['orbit']['theta']},{said['orbit']['phi']},{said['orbit']['radius']}",
                   "--target", f"{said['target']['x']},{said['target']['y']},{said['target']['z']}", "--upright", str(said["verticalFoV"])]
        if said.get("renderSkybox"):
            command += ["--behind"]
        if args.follow:
            command += ["--follow", str(args.follow)]
        done = subprocess.run(command, capture_output=True, text=True, check=False)
        try:
            answer = json.loads(done.stdout)
        except json.JSONDecodeError:
            answer = {"ok": False, "error": done.stderr.strip() or "no answer"}
        if not answer.get("ok"):
            rows.append((name, None, answer.get("error", "failed")))
            failed += 1
            continue
        rows.append((name, answer, ""))
    drawn = [row for row in rows if row[1]]
    within = sum(1 for row in drawn if row[1]["golden"]["decibels"] <= row[1]["golden"]["suite_bound_decibels"])
    print(f"{len(drawn)} scenarios drawn, {within} within the suite's bound (−22 dB), {len(rows) - len(drawn)} not drawn\n")
    head = "| Scenario | From the golden (dB) | The golden's pixels not drawn | Notes |"
    rule = "|---|---|---|---|"
    if args.follow:
        head = "| Scenario | From the golden (dB) | Drawn, from the light followed (dB) | The golden, from the light followed (dB) | The golden's pixels not drawn | Notes |"
        rule = "|---|---|---|---|---|---|"
    print(head)
    print(rule)
    for name, answer, why in sorted(rows, key=lambda row: (row[1] is None, row[1]["golden"]["decibels"] if row[1] else 0)):
        if not answer:
            cells = [name, "—"] + (["—", "—"] if args.follow else []) + ["—", why]
        else:
            cells = [name, f"{answer['golden']['decibels']:.1f}"]
            if args.follow:
                followed = answer.get("followed", {})
                cells += [f"{followed['drawn']['decibels']:.1f}" if "drawn" in followed else "—", f"{followed['golden']['decibels']:.1f}" if "golden" in followed else "—"]
            cells += [str(answer["golden"]["its_pixels_not_drawn"]), f"{answer['model']['notes']} left out" if answer["model"]["notes"] else ""]
        print("| " + " | ".join(cells) + " |")
    return 1 if failed and asked else 0


if __name__ == "__main__":
    sys.exit(main())
