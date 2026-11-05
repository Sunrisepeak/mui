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
import tomllib

NAMES = ("hash", "math", "log", "trace", "testing", "geom", "memory", "unicode", "fuzzy", "session", "pipeline", "image",
         "rhi", "raster", "shader")
GFX = {"rhi", "raster", "shader"}
EXTERNAL = {"mcpplibs.os", "mcpplibs.openkal", "nlohmann.json", "mcpplibs.mwrap.libpng", "mcpplibs.mwrap.libwebp",
            "compat.libpng", "compat.libwebp", "compat.zlib"}


def locked_packages(path: Path) -> dict:
    # Older locks spell openkal keys without their default namespace.
    return {(key if "." in key else data.get("namespace", "mcpplibs") + "." + key): dict(data, lock_key=key)
            for key, data in tomllib.loads(path.read_text())["package"].items()}


def external_source(path: Path, packages: dict) -> bool:
    # Only the exact selected index package/version installation, not an
    # arbitrary source from the registry or another repository package.
    for name, data in packages.items():
        namespace, leaf = name.rsplit(".", 1)
        if any(parent.name == data["version"] and parent.parent.name == namespace + "-x-" + leaf
               for parent in path.parents):
            return True
    return False


def sha(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def identity(name: str) -> str:
    return "mlibs." + name if name in NAMES else name


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
        "format": "mlibs.namespace-consumer@0.1", "ok": False, "commands": [], "packs": [], "runs": [], "rule_runs": [],
        "source_lock_sha256_before": before,
        "limits": ["Source compatibility after rebuild; not C++ binary ABI compatibility or registry aliases.",
                   "Native release source and archive consumers; this gate does not test other targets.",
                   "Fresh consumer build times can reuse global toolchain caches; they are not clean-machine benchmarks.",
                   "The packed consumer explicitly declares foundation dependencies, hanim.core and external index dependencies.",
                   "Shader provisioning is tested on this machine; relocation of the recorded Slang tool path is not verified.",
                   "mcpp index lock hashes describe namespace/name/version, not downloaded artifact content.",
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
        source_lock = locked_packages(repo / "mcpp.lock")
        allowed_external = {name: data for name, data in source_lock.items()
                            if name in EXTERNAL or name.startswith("mcpplibs.openkal-")}
        # mcpp v2026.11.2.2 hashes '<index>:<lock-key>@<version>'; default
        # namespace keys may be short or qualified. Reuse mlibs.hash in the
        # real consumer to check both spellings rather than copying FNV here.
        identity_inputs = sorted({f'{data["namespace"]}:{key}@{data["version"]}'
                                  for name, data in allowed_external.items() for key in (name, name.rsplit(".", 1)[1])})
        external_identities = None
        source_paths = {name: repo / "modules" / ("gfx" if name in GFX else "base") / name for name in NAMES}
        source_paths["hanim.core"] = repo / "modules/base/hanim/core"
        packed_paths = {}
        for name, path in source_paths.items():
            # shader also publishes the omui-shader executable; select its
            # library target so this fixture tests the real archive API.
            target = ["mlibs-shader"] if name == "shader" else []
            stdout = command("pack-" + name, [args.mcpp, "pack", *target, "-p", identity(name),
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
            for module in (("mlibs." + name, "openmui." + name) if name in NAMES else (name,)):
                if "export module " + module + ";" not in text:
                    raise RuntimeError(f"packed public interface is missing {module}")
            if list(packed.rglob("*.cpp")):
                raise RuntimeError("packed consumer must link compiled implementations")
            packed_paths[name] = packed
            shutil.copyfile(manifests[0], artifacts / (name + ".packed.mcpp.toml"))
            report["packs"].append({"name": identity(name), "archive": str(archive), "sha256": sha(archive),
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
                dependencies += '\nhanim.core = { path = ' + json.dumps(str(paths["hanim.core"])) + ' }'
                if source_kind == "packed":
                    # Current pack omits dependency metadata. Supply the actual
                    # direct external edges here without editing the archives.
                    for name in ("mcpplibs.os", "nlohmann.json", "mcpplibs.mwrap.libpng", "mcpplibs.mwrap.libwebp"):
                        version = json.dumps(source_lock[name]["version"])
                        value = '{ version = ' + version + ', features = ["demux", "mux"] }' if name.endswith(".libwebp") else version
                        dependencies += f"\n{name} = {value}"
                imports = "\n".join(f"import {prefix}.{name};" for prefix in prefixes for name in NAMES)
                (project / "mcpp.toml").write_text((fixture / "mcpp.toml.in").read_text().replace("{{dependencies}}", dependencies))
                (project / "src/main.cpp").write_text((fixture / "main.cpp.in").read_text().replace("{{imports}}", imports)
                                                      .replace("{{lock_identities}}", ", ".join(map(json.dumps, identity_inputs))))
                shutil.copyfile(fixture / "consumer.slang", project / "consumer.slang")
                argv = [args.mcpp, "run", "--profile", "release"]
                stdout = command(label, argv, project, bootstrap=True)
                result = [json.loads(line) for line in stdout.splitlines() if line.startswith('{"kind":"mlibs.namespace-consumer"')]
                if len(result) != 1 or not result[0].get("ok") or result[0].get("cases") != 15:
                    raise RuntimeError(label + ": missing real successful consumer result")
                own_lock = sha(project / "mcpp.lock")
                command(label + "-incremental", argv + ["--locked"], project)
                if sha(project / "mcpp.lock") != own_lock:
                    raise RuntimeError("consumer lock changed on incremental run")
                command(label + "-metadata", [args.mcpp, "build", "--profile", "release", "--locked", "--configure-only"], project)
                resolution = max((project / "target").glob("*/*/resolution.json"), key=lambda p: p.stat().st_mtime_ns)
                graph = json.loads(resolution.read_text())
                shutil.copyfile(resolution, artifacts / (label + ".resolution.json"))
                shutil.copyfile(project / "mcpp.lock", artifacts / (label + ".mcpp.lock"))
                if graph.get("schema_version") != 2:
                    raise RuntimeError("unsupported mcpp resolution schema")
                packages = graph["graph"]["packages"]
                identities = []
                external_packages = {}
                consumer_lock = locked_packages(project / "mcpp.lock")
                for package in packages:
                    data = package["package"]
                    package_id = (data.get("namespace", "") + "." + data["name"]).lstrip(".")
                    identities.append(package_id)
                    if package_id in allowed_external:
                        pinned = allowed_external[package_id]
                        if any(data.get(key) != pinned[key] or consumer_lock.get(package_id, {}).get(key) != pinned[key]
                               for key in ("version", "source")):
                            raise RuntimeError(label + ": external dependency differs from repository lock: " + package_id +
                                               "; expected=" + json.dumps(pinned) + "; resolved=" + json.dumps(data) +
                                               "; lock=" + json.dumps(consumer_lock.get(package_id)))
                        for locked in (pinned, consumer_lock[package_id]):
                            key = f'{locked["namespace"]}:{locked["lock_key"]}@{locked["version"]}'
                            if locked.get("hash") != result[0]["identity_hashes"].get(key):
                                raise RuntimeError(label + ": invalid index identity digest: " + key)
                        external_packages[package_id] = data
                        continue
                    expected_path = project if package_id == "mlibs.validation.namespace_consumer" else {identity(k): p for k, p in paths.items()}.get(package_id)
                    if expected_path is None or data.get("source") != "path+" + str(expected_path):
                        raise RuntimeError(label + ": unexpected dependency source " + str(data))
                if not EXTERNAL.issubset(external_packages) or len(set(external_packages) - EXTERNAL) != 1:
                    raise RuntimeError(label + ": incomplete external closure or multiple OS backends")
                if external_identities is None:
                    external_identities = set(external_packages)
                if set(external_packages) != external_identities:
                    raise RuntimeError(label + ": external closure changed between consumer styles")
                expected = {"mlibs.validation.namespace_consumer", *map(identity, paths), *external_identities}
                if set(identities) != expected or len(identities) != len(expected):
                    raise RuntimeError(label + ": duplicate or unexpected package identities")
                cdb = json.loads((resolution.parent / "compile_commands.json").read_text())
                shutil.copyfile(resolution.parent / "compile_commands.json", artifacts / (label + ".compile_commands.json"))
                sources = []
                external_sources = []
                generated_sources = []
                shader_package = next(p for p in packages if p["package"]["canonical"].startswith("mlibs.shader@"))
                provisioned = (Path(shader_package["outDir"]) / "mlibs/slang_provisioned.cppm").resolve()
                for entry in cdb:
                    directory = Path(entry["directory"])
                    if not directory.is_absolute():
                        directory = project / directory
                    path = Path(entry["file"])
                    path = (directory / path).resolve() if not path.is_absolute() else path.resolve()
                    if path == (project / "src/main.cpp") or any(path.is_relative_to(root) for root in paths.values()):
                        sources.append(str(path))
                    elif source_kind == "source" and path == provisioned:
                        if "export module mlibs.shader.api:provisioned;" not in path.read_text():
                            raise RuntimeError(label + ": incorrect generated shader interface")
                        generated_sources.append({"path": str(path), "sha256": sha(path)})
                    elif path.name in {"std.cc", "std.cppm"} and path.is_relative_to(Path(entry["arguments"][0]).resolve().parent.parent):
                        continue  # the selected compiler's standard-library module
                    elif external_source(path, external_packages):
                        external_sources.append(str(path))
                    else:
                        raise RuntimeError(label + ": unexpected compiled source " + str(path))
                if len(sources) != len(set(sources)) or len(external_sources) != len(set(external_sources)):
                    raise RuntimeError(label + ": duplicated implementation compilation")
                mains = {str((root / target["main"]).resolve()) for root in source_paths.values()
                         for target in tomllib.loads((root / "mcpp.toml").read_text()).get("targets", {}).values() if "main" in target}
                tool_sources = sorted(set(sources) & mains)
                implementations = [p for p in sources if p.endswith(".cpp") and p != str(project / "src/main.cpp") and p not in mains]
                if source_kind == "packed" and implementations:
                    raise RuntimeError(label + ": binary consumer silently built source implementations")
                if source_kind == "source":
                    # mcpp configures/compiles the dependency package's CLI
                    # entry too. It must not be linked into this consumer.
                    links = [line for line in (resolution.parent / "build.ninja").read_text().splitlines()
                             if line.startswith("build ") and "bin/namespace-consumer" in line.partition(":")[0].split()[1:]]
                    if len(links) != 1 or set(tool_sources) != mains:
                        raise RuntimeError(label + ": missing unique consumer link or declared tool entry; links=" +
                                           str(len(links)) + "; tools=" + str(tool_sources) + "; expected=" + str(sorted(mains)))
                    for entry in cdb:
                        if str(Path(entry["file"]).resolve()) in mains:
                            obj = Path(entry["output"]).relative_to(resolution.parent).as_posix()
                            if obj in links[0].split():
                                raise RuntimeError(label + ": dependency CLI entry linked into consumer")
                expected_implementations = {str(p.resolve()) for root in source_paths.values()
                                            for p in (root / "src").rglob("*.cpp")} - mains
                if source_kind == "source" and set(implementations) != expected_implementations:
                    raise RuntimeError(label + ": missing or unexpected implementation units; missing=" +
                                       str(sorted(expected_implementations - set(implementations))) + "; extra=" +
                                       str(sorted(set(implementations) - expected_implementations)))
                if source_kind == "source" and len(generated_sources) != 1:
                    raise RuntimeError(label + ": missing generated Slang provisioning interface")
                shutil.copyfile(resolution, artifacts / (label + ".resolution.json"))
                shutil.copyfile(project / "mcpp.toml", artifacts / (label + ".mcpp.toml"))
                report["runs"].append({"kind": source_kind, "style": style, "result": result[0],
                                       "packages": sorted(identities), "compiled_sources": sources,
                                       "declared_tool_sources_excluded_from_consumer_link": tool_sources,
                                       "external_sources": external_sources, "external_packages": external_packages,
                                       "generated_sources": generated_sources,
                                       "toolchain": graph["toolchain"]})
        # Host modules are build-time plugins, not runtime library archives.
        # Exercise their new/old imports with real out-of-workspace generated
        # partitions, then compile and read the resulting shader constants.
        for style in ("canonical", "legacy", "mixed"):
            label = "rules-" + style
            project = work / label
            (project / "src").mkdir(parents=True)
            prefixes = ("mlibs", "openmui") if style == "mixed" else (("mlibs",) if style == "canonical" else ("openmui",))
            dependencies = "\n".join(f'{prefix}.shader.rules = {{ path = {json.dumps(str(repo / "modules/gfx/shader/rules"))}, host-module = true }}'
                                     for prefix in prefixes)
            (project / "mcpp.toml").write_text((fixture / "rules/mcpp.toml.in").read_text()
                .replace("{{rhi_path}}", json.dumps(str(repo / "modules/gfx/rhi"))).replace("{{dependencies}}", dependencies))
            (project / "build.mcpp").write_text((fixture / "rules/build.mcpp.in").read_text()
                .replace("{{imports}}", "\n".join(f"import {prefix}.shader.rules;" for prefix in prefixes))
                .replace("{{rule_namespace}}", ("openmui" if style != "canonical" else "mlibs") + "::shader::rules"))
            for name in ("main.cpp", "consumer.cppm"):
                shutil.copyfile(fixture / "rules" / name, project / "src" / name)
            shutil.copyfile(fixture / "consumer.slang", project / "consumer.slang")
            stdout = command(label, [args.mcpp, "run", "--profile", "release"], project, bootstrap=True)
            results = [json.loads(line) for line in stdout.splitlines() if line.startswith('{"kind":"mlibs.rules-consumer"')]
            if len(results) != 1 or not results[0].get("ok") or results[0].get("entries") != 2:
                raise RuntimeError(label + ": generated shader program did not execute")
            command(label + "-incremental", [args.mcpp, "run", "--profile", "release", "--locked"], project)
            generated = list((project / "target/.build-mcpp/out").glob("*/mlibs.validation.rules_consumer/mlibs/shader/consumer_shaders.cppm"))
            if len(generated) != 1 or "import mlibs.rhi;" not in generated[0].read_text():
                raise RuntimeError(label + ": missing unique canonical generated partition")
            shutil.copyfile(generated[0], artifacts / (label + ".generated.cppm"))
            report["rule_runs"].append({"style": style, "result": results[0], "generated_sha256": sha(generated[0])})
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
