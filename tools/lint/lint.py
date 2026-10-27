#!/usr/bin/env python3
"""openmui's repository rules, checked (AGENTS.md; implementation plan §15.3).

    python3 tools/lint/lint.py            check the repository
    python3 tools/lint/lint.py --self-test show that each rule catches a violation

Rules:
  macros        no `#define` in our sources (spec/include/openmui.h is the one exception)
  headers       no header files but spec/include/openmui.h; `#include` only in `native/` units
  naming        no bare `mui` identifier: the identity is `openmui`, the abbreviation `omui`
  layering      a member depends only on the layers below it (dependency table)
  manifest      namespaced dependencies are dotted keys (`compat.sdl3 = "…"`) of [dependencies]
  pins          a graph-wide pin of the root ([package.metadata.openmui.pins]) is declared, the same,
                by every member whose graph reaches the package it is for
  modules       a member of modules/ depends on no openmui package outside modules/
  exceptions    no `throw` / `try` in runtime and modules sources (errors are std::expected)
  paths         no std::filesystem::path::string() / generic_string() outside tests: on Windows
                they convert through the ANSI code page and throw on a name it cannot spell; use
                utf8() / generic_utf8() (mcpp .agents/skills/mcpp-contributing, 路径窄化不变式);
                `// NARROW-OK: <why>` on the line allows one
  platform      no operating-system header anywhere: platform code goes through mcpplibs.os
  determinism   deterministic members do not call <cmath>'s transcendental functions and
                compile with -ffp-contract=off
  members       every directory listed as a workspace member has an mcpp.toml, and every
                mcpp.toml under the tree is listed
  documents     every view document (*.view.omui.json) is valid against the committed schema,
                spec/schema/view.schema.json (`omui schema`); needs the `jsonschema` package
                (CI installs it; without it the rule says it was skipped)
"""
from __future__ import annotations

import fnmatch
import re
import shutil
import sys
import tempfile
import tomllib
from dataclasses import dataclass
from pathlib import Path

SOURCE_SUFFIXES = {".cppm", ".cpp", ".ixx", ".cc", ".cxx", ".h", ".hpp", ".hh", ".hxx", ".inl"}
HEADER_SUFFIXES = {".h", ".hpp", ".hh", ".hxx", ".inl"}
THE_HEADER = Path("spec/include/openmui.h")
SKIP_DIRS = {"target", ".git", ".cache", "build", "node_modules", "fixtures", ".venv", "__pycache__"}

# Layer of a member, by its directory. A member may depend on members of the
# same or a lower layer, as listed here (the dependency table of the
# architecture design, §2).
LAYERS: list[tuple[str, set[str]]] = [
    ("tests/support", set()),
    ("modules/*", {"modules"}),
    ("spec", set()),
    ("runtime/*", {"modules", "spec", "runtime"}),
    ("hosts/*", {"modules", "spec", "runtime", "hosts"}),
    ("components/*", {"modules", "spec", "runtime", "components", "themes"}),
    ("themes/*", {"modules", "spec", "runtime", "themes"}),
    ("video", {"modules", "spec", "runtime", "hosts", "components", "themes"}),
    ("lang/*", {"spec", "runtime", "lang"}),
    ("tools/*", {"*"}),
    ("mcpp/*", {"tools"}),  # build code only: the rule and the generator it runs
    ("examples/*", {"*"}),
    ("showcase/*", {"*"}),
    ("tests/*", {"*"}),
    (".", {"*"}),
]

# Members whose results are part of A-level conformance (identical bits).
DETERMINISTIC = ["modules/anim", "modules/raster", "modules/layout", "modules/geom", "modules/render", "modules/text",
                 "runtime/view"]

# Operating-system headers: platform code goes through mcpplibs.os (D-57), so
# none of ours includes them, native units included.
OS_HEADERS = re.compile(
    r"#\s*include\s*[<\"](windows\.h|winsock2?\.h|unistd\.h|pthread\.h|dlfcn\.h|fcntl\.h|sys/[^>\"]+|mach/[^>\"]+|"
    r"CoreFoundation/[^>\"]+|Cocoa/[^>\"]+|AppKit/[^>\"]+|linux/[^>\"]+)[>\"]")
TRANSCENDENTAL = re.compile(
    r"\bstd::(exp|exp2|expm1|log|log2|log10|log1p|pow|sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|cbrt|hypot|lerp)\s*\(")


@dataclass
class Problem:
    rule: str
    where: str
    message: str

    def __str__(self) -> str:
        return f"[{self.rule}] {self.where}: {self.message}"


def layer_of(member: str) -> str:
    return member.split("/")[0] if "/" in member else member


def rule_for(member: str) -> set[str] | None:
    for pattern, allowed in LAYERS:
        if fnmatch.fnmatch(member, pattern):
            return allowed
    return None


def source_files(root: Path):
    for path in sorted(root.rglob("*")):
        if any(part in SKIP_DIRS for part in path.relative_to(root).parts):
            continue
        if path.is_file() and path.suffix in SOURCE_SUFFIXES:
            yield path


def strip_comments(text: str) -> str:
    """Removes // and /* */ comments and string literals, keeping line numbers."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c == 'R' and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            # A raw string literal: R"delim( ... )delim"
            open_paren = text.find("(", i + 2)
            delim = text[i + 2:open_paren]
            close = text.find(")" + delim + '"', open_paren + 1)
            j = n if close < 0 else close + len(delim) + 2
            out.append('""' + "\n" * text.count("\n", i, j))
            i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append('""')
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def workspace_members(root: Path) -> list[str]:
    manifest = root / "mcpp.toml"
    if not manifest.exists():
        return []
    data = tomllib.loads(manifest.read_text(encoding="utf-8"))
    members = []
    for pattern in data.get("workspace", {}).get("members", []):
        if any(ch in pattern for ch in "*?["):
            for d in sorted(root.glob(pattern)):
                if (d / "mcpp.toml").exists():
                    members.append(d.relative_to(root).as_posix())
        else:
            members.append(pattern)
    return members


def check_sources(root: Path) -> list[Problem]:
    problems = []
    for path in source_files(root):
        rel = path.relative_to(root)
        rel_s = rel.as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        code = strip_comments(text)
        lines = code.splitlines()
        is_the_header = rel == THE_HEADER

        if path.suffix in HEADER_SUFFIXES and not is_the_header:
            problems.append(Problem("headers", rel_s, "a header file; openmui has one header (openmui.h), the rest are modules"))

        in_native = "native" in rel.parts
        for no, line in enumerate(lines, 1):
            stripped = line.strip()
            if re.match(r"#\s*define\b", stripped) and not is_the_header:
                problems.append(Problem("macros", f"{rel_s}:{no}", "#define; use constexpr, consteval or a module partition"))
            if re.search(r"\bstd::(getenv|system|_Exit)\s*\(|\bgetenv\s*\(", line) or "/proc/" in text.splitlines()[no - 1]:
                problems.append(Problem("platform", f"{rel_s}:{no}", "environment and process access go through mcpplibs.os (env, process)"))
            if OS_HEADERS.match(stripped):
                problems.append(Problem("platform", f"{rel_s}:{no}", "an operating-system header; use mcpplibs.os"))
            if re.match(r"#\s*include\b", stripped) and not in_native and not is_the_header:
                problems.append(Problem("headers", f"{rel_s}:{no}", "#include outside a native/ unit; import a module instead"))
            if re.search(r"\bmui(::|_[a-z])|\bMUI_|\bnamespace\s+mui\b|\bmodule\s+mui\b|import\s+mui\b", line):
                problems.append(Problem("naming", f"{rel_s}:{no}", "bare `mui`; write `openmui` (identity) or `omui` (abbreviation)"))

        top = rel.parts[0] if rel.parts else ""
        is_test = "tests" in rel.parts
        if top in ("runtime", "modules") and not is_test:
            for no, line in enumerate(lines, 1):
                if re.search(r"\bthrow\b|\btry\s*\{|\bcatch\s*\(", line):
                    problems.append(Problem("exceptions", f"{rel_s}:{no}", "exceptions in core code; return std::expected"))

        # Paths are never narrowed through the code page (outside tests).
        if "/tests/" not in rel_s and not rel_s.startswith("tests/"):
            for no, line in enumerate(lines, 1):
                if re.search(r"\.(generic_)?string\(\)", line) and "NARROW-OK:" not in line:
                    problems.append(Problem("paths", f"{rel_s}:{no}",
                                            "path::string() narrows through the code page on Windows; use utf8() / generic_utf8()"))

        for member in DETERMINISTIC:
            if rel_s.startswith(member + "/src/"):
                for no, line in enumerate(lines, 1):
                    m = TRANSCENDENTAL.search(line)
                    if m:
                        problems.append(Problem("determinism", f"{rel_s}:{no}",
                                                f"std::{m.group(1)} differs between platforms' libm; use omui::anim::dm"))
    return problems


def check_manifests(root: Path) -> list[Problem]:
    problems = []
    members = workspace_members(root)
    listed = set(members)

    for manifest in sorted(root.rglob("mcpp.toml")):
        rel = manifest.parent.relative_to(root).as_posix()
        if any(part in SKIP_DIRS for part in manifest.parent.relative_to(root).parts):
            continue
        if rel in (".",) or rel in listed:
            continue
        if rel.startswith("templates/"):
            continue
        problems.append(Problem("members", f"{rel}/mcpp.toml", "a package that is not a workspace member"))

    for member in members:
        manifest = root / member / "mcpp.toml"
        if not manifest.exists():
            problems.append(Problem("members", member, "listed as a member, but has no mcpp.toml"))
            continue
        try:
            data = tomllib.loads(manifest.read_text(encoding="utf-8"))
        except tomllib.TOMLDecodeError as e:
            problems.append(Problem("members", member, f"mcpp.toml is not valid TOML: {e}"))
            continue
        # Namespaced dependencies are dotted keys of the one table
        # (`compat.sdl3 = "3.4.18"`), never tables of their own.
        for table in ("dependencies", "dev-dependencies"):
            for line in manifest.read_text(encoding="utf-8").splitlines():
                m = re.match(r"^\[" + table + r"\.([A-Za-z0-9_\-]+)\]\s*$", line)
                if m:
                    problems.append(Problem("manifest", member,
                                            f"[{table}.{m.group(1)}]: write `{m.group(1)}.<name> = ...` under [{table}]"))
        allowed = rule_for(member)
        if allowed is None:
            problems.append(Problem("layering", member, "a member in no known layer; add it to tools/lint/lint.py"))
            continue
        tables = [data.get("dependencies", {})]
        for target in data.get("target", {}).values():
            if isinstance(target, dict):
                tables.append(target.get("dependencies", {}))
        for table in tables:
            for name, spec in flatten_deps(table):
                if not isinstance(spec, dict) or "path" not in spec:
                    continue
                dep = (manifest.parent / spec["path"]).resolve()
                try:
                    dep_rel = dep.relative_to(root.resolve()).as_posix()
                except ValueError:
                    continue
                dep_layer = layer_of(dep_rel) if dep_rel != "" else "."
                if dep_rel.startswith("tests/support"):
                    problems.append(Problem("layering", member, f"`{name}`: openmui.testing belongs in [dev-dependencies]"))
                    continue
                if "*" in allowed:
                    continue
                if dep_layer not in allowed:
                    problems.append(Problem("layering" if not member.startswith("modules/") else "modules",
                                            member, f"depends on {dep_rel} ({name}); allowed layers: {sorted(allowed) or 'std only'}"))

        if member in DETERMINISTIC:
            flags = " ".join(data.get("build", {}).get("cxxflags", []))
            if "-ffp-contract=off" not in flags:
                problems.append(Problem("determinism", f"{member}/mcpp.toml", "missing -ffp-contract=off in [build] cxxflags"))
    return problems


def check_pins(root: Path) -> list[Problem]:
    problems = []
    manifest = root / "mcpp.toml"
    if not manifest.exists():
        return problems
    data = tomllib.loads(manifest.read_text(encoding="utf-8"))
    pins = data.get("package", {}).get("metadata", {}).get("openmui", {}).get("pins", {})
    if not pins:
        return problems
    root_deps = dict(flatten_deps(data.get("dependencies", {})))
    loaded: dict[str, dict] = {}

    def deps_of(member: str) -> dict:
        if member not in loaded:
            path = root / member / "mcpp.toml"
            try:
                d = tomllib.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
            except tomllib.TOMLDecodeError:
                d = {}
            out = {}
            for table in ("dependencies", "dev-dependencies"):
                out.update(dict(flatten_deps(d.get(table, {}))))
            for target in d.get("target", {}).values():
                if isinstance(target, dict):
                    out.update(dict(flatten_deps(target.get("dependencies", {}))))
            loaded[member] = out
        return loaded[member]

    def reaches(member: str, package: str, seen: set) -> bool:
        if member in seen:
            return False
        seen.add(member)
        for name, spec in deps_of(member).items():
            if name == package:
                return True
            if isinstance(spec, dict) and "path" in spec:
                dep = (root / member / spec["path"]).resolve()
                try:
                    rel = dep.relative_to(root.resolve()).as_posix() or "."
                except ValueError:
                    continue
                if reaches(rel, package, seen):
                    return True
        return False

    # A template's project depends on openmui itself, so it reaches every pinned package.
    templates = {}
    for t in sorted((root / "templates").glob("*/mcpp.toml.in")) if (root / "templates").exists() else []:
        try:
            # Placeholders as mcpp renders them (`{{project.name}}` → a name).
            text = re.sub(r"\{\{[^}]*\}\}", "placeholder", t.read_text(encoding="utf-8"))
            templates[t.relative_to(root).as_posix()] = dict(flatten_deps(tomllib.loads(text).get("dependencies", {})))
        except tomllib.TOMLDecodeError as e:
            problems.append(Problem("pins", t.relative_to(root).as_posix(), f"not valid TOML: {e}"))

    for pinned, package in pins.items():
        want = root_deps.get(pinned)
        if want is None:
            problems.append(Problem("pins", "mcpp.toml", f"pins `{pinned}` but does not declare it in [dependencies]"))
            continue
        for template, tdeps in templates.items():
            if tdeps.get(pinned) != want:
                problems.append(Problem("pins", template, f"an openmui project reaches {package}: declare `{pinned} = {want}`"))
        for member in workspace_members(root):
            if not reaches(member, package, set()):
                continue
            have = deps_of(member).get(pinned)
            if have != want:
                problems.append(Problem("pins", member, f"reaches {package}: declare `{pinned} = {want}` as the root does"
                                        + ("" if have is None else f" (it has {have})")))
    return problems


def flatten_deps(table: dict, prefix: str = ""):
    for key, value in table.items():
        if isinstance(value, dict) and not any(k in value for k in ("path", "git", "version", "workspace")):
            yield from flatten_deps(value, prefix + key + ".")
        else:
            yield prefix + key, value


def check_documents(root: Path) -> list[Problem]:
    try:
        import jsonschema  # type: ignore
    except ImportError:
        print("lint: documents skipped (pip install jsonschema)", file=sys.stderr)
        return []
    import json
    schema_file = root / "spec" / "schema" / "view.schema.json"
    docs = [f for f in root.rglob("*.view.omui.json") if not (set(f.relative_to(root).parts) & SKIP_DIRS)]
    if not docs:
        return []
    if not schema_file.exists():
        return [Problem("documents", "spec/schema/view.schema.json", "missing: omui schema --out spec/schema/view.schema.json")]
    validator = jsonschema.Draft202012Validator(json.loads(schema_file.read_text(encoding="utf-8")))
    problems = []
    for f in sorted(docs):
        where = f.relative_to(root).as_posix()
        try:
            doc = json.loads(f.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            problems.append(Problem("documents", f"{where}:{e.lineno}:{e.colno}", e.msg))
            continue
        for e in sorted(validator.iter_errors(doc), key=lambda e: list(e.absolute_path)):
            pointer = "/" + "/".join(str(x) for x in e.absolute_path)
            problems.append(Problem("documents", f"{where} {pointer}", e.message[:200]))
    return problems


def run(root: Path) -> list[Problem]:
    return check_sources(root) + check_manifests(root) + check_pins(root) + check_documents(root)


def self_test() -> int:
    """Each rule must reject a violation planted in a copy of a minimal tree."""
    cases = {
        "macros": ("modules/anim/src/bad.cpp", "module x;\n#define BAD 1\n"),
        "headers": ("runtime/core/src/util.h", "int f();\n"),
        "naming": ("runtime/core/src/a.cpp", "namespace mui { }\n"),
        "exceptions": ("modules/anim/src/e.cpp", "void f() { throw 1; }\n"),
        "paths": ("runtime/core/src/p.cpp", "std::string f(const std::filesystem::path& p) { return p.string(); }\n"),
        "determinism": ("modules/anim/src/d.cpp", "double f(double x) { return std::sin(x); }\n"),
        "platform": ("runtime/core/src/native/io.cpp", "module;\n#include <unistd.h>\nmodule x;\n"),
        "layering": None,
        "modules": None,
        "manifest": None,
        "pins": None,
        "documents": ("examples/x/ui/bad.view.omui.json", '{"omui": "0.1", "view": {"root": {"typ": "text"}}}'),
    }
    try:
        import jsonschema  # type: ignore  # noqa: F401
    except ImportError:
        del cases["documents"]
        print("  skip  documents: pip install jsonschema")
    failures = 0
    for rule, planted in cases.items():
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            members = ["modules/anim", "runtime/core", "spec"]
            pin = ""
            if rule == "pins":
                pin = ('[package]\nname = "root"\nversion = "0.1.0"\n[package.metadata.openmui.pins]\n"ns.kernel" = "ns.os"\n'
                       '[dependencies]\nns.kernel = "1.0.0"\n')
            (root / "mcpp.toml").write_text(
                pin + "[workspace]\nmembers = [" + ", ".join(f'"{m}"' for m in members) + "]\n", encoding="utf-8")
            for m in members:
                (root / m / "src").mkdir(parents=True)
                deps = ""
                if rule == "layering" and m == "spec":
                    deps = '[dependencies]\ncore = { path = "../runtime/core" }\n'
                if rule == "modules" and m == "modules/anim":
                    deps = '[dependencies]\ncore = { path = "../../runtime/core" }\n'
                if rule == "manifest" and m == "runtime/core":
                    deps = '[dependencies.compat]\nsdl3 = "3.4.18"\n'
                if rule == "pins" and m == "runtime/core":
                    deps = '[dependencies]\nns.os = "0.1.0"\n'  # reaches ns.os, does not pin ns.kernel
                flags = '[build]\ncxxflags = ["-ffp-contract=off"]\n' if m == "modules/anim" else ""
                (root / m / "mcpp.toml").write_text(f'[package]\nname = "{m}"\nversion = "0.1.0"\n{flags}{deps}',
                                                    encoding="utf-8")
            if rule == "documents":
                (root / "spec" / "schema").mkdir(parents=True, exist_ok=True)
                shutil.copy(Path(__file__).resolve().parents[2] / "spec" / "schema" / "view.schema.json",
                            root / "spec" / "schema" / "view.schema.json")
            if planted:
                path, text = planted
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_text(text, encoding="utf-8")
            found = [p for p in run(root) if p.rule == rule]
            clean = [p for p in run(root) if p.rule != rule]
            ok = bool(found) and not clean
            print(f"  {'ok  ' if ok else 'FAIL'}  {rule}: {found[0] if found else 'not caught'}")
            if clean:
                for p in clean:
                    print(f"        unexpected: {p}")
            failures += 0 if ok else 1
    print(f"lint self-test: {len(cases) - failures} of {len(cases)} rules catch their violation")
    return 1 if failures else 0


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()
    root = Path(__file__).resolve().parents[2]
    problems = run(root)
    for p in problems:
        print(p)
    print(f"lint: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
