#!/usr/bin/env python3
"""openmui's repository rules, checked (AGENTS.md; implementation plan §15.3).

    python3 tools/lint/lint.py            check the repository
    python3 tools/lint/lint.py --self-test show that each rule catches a violation

Rules:
  macros        no `#define` in our sources (spec/include/openmui.h is the one exception)
  headers       no header files but spec/include/openmui.h; `#include` only in `native/` units
  naming        no bare `mui` identifier: the identity is `openmui`, the abbreviation `omui`
  layering      a member depends only on the layers below it (D-82: the independent components,
                openmui, mgame; gongfu's core on the standard library alone)
  manifest      namespaced dependencies are dotted keys (`compat.sdl3 = "…"`) of [dependencies]
  pins          a graph-wide pin of the root ([package.metadata.openmui.pins]) is declared, the same,
                by every member whose graph reaches the package it is for
  modules       an independent component (modules/{base,gfx,content,media}) depends on no openmui
                package outside them (D-55)
  layout        a library's directory is its package's name, a package inside another is named after it,
                and every directory with packages under it has a README.md that links each (D-80)
  licenses      every index package the workspace reaches is in tools/lint/licenses.toml; none is GPL
                or AGPL; LGPL only under [dynamic] (optional, linked dynamically); the licences match
                a local index's descriptors (D-83)
  platforms     a package that says it runs on the Web (platforms lists "emscripten") depends only on
                packages that say so too (dev-dependencies included) (D-84)
  media         a picture in the repository (.png .apng .gif .jpg .webp) is under 1 MB, and every showcase's
                README shows its signature shot from its media/ (tools/ci/readme-media.sh makes them)
  thirdparty    a wrapped C library (`import mcpplibs.mwrap.*`) only in a native/ unit (D-83)
  exceptions    no `throw` / `try` in runtime and modules sources (errors are std::expected); a
                native/ unit may catch what a third-party library throws, to answer std::expected
  paths         no std::filesystem::path::string() / generic_string() outside tests: on Windows
                they convert through the ANSI code page and throw on a name it cannot spell; use
                utf8() / generic_utf8() (mcpp .agents/skills/mcpp-contributing, 路径窄化不变式);
                `// NARROW-OK: <why>` on the line allows one
  platform      no operating-system header anywhere: platform code goes through mcpplibs.os
  determinism   deterministic members do not call <cmath>'s transcendental functions and
                compile with -ffp-contract=off
  hash          no private FNV-1a (its offset bases and primes) outside modules/base/hash: one
                implementation, openmui.hash, so a pinned hash means the same thing everywhere
  math          no private splitmix64 (its finaliser's constants) or fdlibm (its constants)
                outside modules/base/math: one implementation, openmui.math (D-87)
  members       every directory listed as a workspace member has an mcpp.toml, and every
                mcpp.toml under the tree is listed
  documents     every view document (*.view.omui.json) is valid against the committed schema,
                spec/schema/view.schema.json (`omui schema`), and every CTS scenario and trace
                (*.scenario.omui.json, *.trace.omui.json) against spec/schema/scenario.schema.json;
                needs the `jsonschema` package
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

# Layers (D-80, D-82). A member's layer is the most specific group its
# directory lies in; it may depend on the layers listed for it (path
# dependencies in [dependencies] and target dependencies). The independent
# components (modules/{base,gfx,content,media}) know nothing of openmui
# (D-55); openmui (modules/ui) never depends on the game engine (mgame); the
# fighting framework's core (mgame/gongfu) depends on the standard library
# alone, so that it can leave the repository unchanged.
GROUPS = ["modules/base/hash", "modules/base/testing", "modules/base", "modules/gfx", "modules/content", "modules/media", "modules/ui/host", "modules/ui",
          "spec", "components", "lang", "mgame/gongfu", "mgame", "tools", "mcpp", "src", "tests", "examples", "showcase", "site", "."]
INDEPENDENT = ["modules/base", "modules/gfx", "modules/content", "modules/media"]
_BELOW_UI = {"modules/base", "modules/gfx", "modules/content", "modules/media", "spec"}
LAYERS: dict[str, set[str]] = {
    "modules/base/hash": set(),  # the bottom: what testing hashes with
    "modules/base/testing": {"modules/base/hash"},
    "modules/base": {"modules/base"},
    "modules/gfx": {"modules/base", "modules/gfx"},
    "modules/content": {"modules/base", "modules/gfx", "modules/content"},
    "modules/media": {"modules/base", "modules/gfx", "modules/content", "modules/media"},
    "spec": set(),
    "modules/ui": _BELOW_UI | {"modules/ui"},
    "modules/ui/host": _BELOW_UI | {"modules/ui", "modules/ui/host"},
    "components": _BELOW_UI | {"modules/ui", "components"},
    "lang": {"spec", "modules/ui", "components", "lang"},
    "mgame/gongfu": set(),
    "mgame": _BELOW_UI | {"modules/ui", "modules/ui/host", "components", "mgame", "mgame/gongfu"},
    "tools": {"*"},
    "mcpp": {"tools", "mcpp"},  # build code only: the plugins, their shared base and the generator they run
    "src": {"*"},  # entries: the composition roots (component-binding §10.2)
    "tests": {"*"},
    "examples": {"*"},
    "showcase": {"*"},
    "site": {"*"},
    ".": {"*"},
}
# openmui.hash is the base layer's bottom: whoever may use the base may use it.
for _allowed in LAYERS.values():
    if "modules/base" in _allowed:
        _allowed.add("modules/base/hash")

# Members whose results are part of A-level conformance (identical bits).
DETERMINISTIC = ["modules/base/math", "modules/base/fuzzy", "modules/media/audio", "modules/media/video", "mgame/physics", "showcase/widget-breakout/sim", "modules/base/hanim", "modules/gfx/raster", "modules/content/layout", "modules/base/geom", "modules/gfx/render", "modules/gfx/render/vector", "modules/content/text",
                 "modules/content/i18n", "modules/ui/view", "showcase/promo-video"]

# Operating-system headers: platform code goes through mcpplibs.os (D-57), so
# none of ours includes them, native units included.
OS_HEADERS = re.compile(
    r"#\s*include\s*[<\"](windows\.h|winsock2?\.h|unistd\.h|pthread\.h|dlfcn\.h|fcntl\.h|sys/[^>\"]+|mach/[^>\"]+|"
    r"CoreFoundation/[^>\"]+|Cocoa/[^>\"]+|AppKit/[^>\"]+|linux/[^>\"]+)[>\"]")
# FNV-1a's offset bases and primes, 64- and 32-bit, in hex and in decimal
# (and the 64-bit basis with its last digit lost, as copies had it): only
# openmui.hash has them.
FNV_CONSTANTS = re.compile(r"0x100000001b3|0xcbf29ce484222325|\b1469598103934665603[0-9]?|\b1099511628211\b|\b16777619u?\b|\b2166136261u?\b",
                           re.IGNORECASE)
# What may keep its own: gongfu's core takes the standard library alone (D-82),
# so its world checksum is written there.
FNV_OWN = ("modules/base/hash/", "mgame/gongfu/src/")

# splitmix64's finaliser and fdlibm's exp/log constants: only openmui.math has them.
MATH_CONSTANTS = re.compile(r"0xbf58476d1ce4e5b9|0x94d049bb133111eb|6\.93147180369123816490e-01|1\.66666666666666019037e-01", re.IGNORECASE)

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
    """The most specific group a directory lies in ("mgame/gongfu" is the core alone)."""
    if member == "mgame/gongfu":
        return member
    for g in GROUPS:
        if g in ("mgame/gongfu", "."):
            continue
        if member == g or member.startswith(g + "/"):
            return g
    return "."


def rule_for(member: str) -> set[str] | None:
    return LAYERS.get(layer_of(member))


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
            if re.match(r"(export\s+)?import\s+mcpplibs\.mwrap\.", stripped) and not in_native and "tests" not in rel.parts:
                problems.append(Problem("thirdparty", f"{rel_s}:{no}",
                                        "a wrapped C library (mcpplibs.mwrap.*) is used only in a native/ unit (D-83)"))
            if re.search(r"\bmui(::|_[a-z])|\bMUI_|\bnamespace\s+mui\b|\bmodule\s+mui\b|import\s+mui\b", line):
                problems.append(Problem("naming", f"{rel_s}:{no}", "bare `mui`; write `openmui` (identity) or `omui` (abbreviation)"))

        top = rel.parts[0] if rel.parts else ""
        is_test = "tests" in rel.parts
        if top in ("modules", "mgame") and not is_test:
            # A native/ unit adapts a third-party library as it adapts its
            # macros: it may catch what the library throws, to answer
            # std::expected. Nothing in core code throws.
            pattern = r"\bthrow\b" if in_native else r"\bthrow\b|\btry\s*\{|\bcatch\s*\("
            for no, line in enumerate(lines, 1):
                if re.search(pattern, line):
                    problems.append(Problem("exceptions", f"{rel_s}:{no}", "exceptions in core code; return std::expected"))

        # Paths are never narrowed through the code page (outside tests).
        if "/tests/" not in rel_s and not rel_s.startswith("tests/"):
            for no, line in enumerate(lines, 1):
                if re.search(r"\.(generic_)?string\(\)", line) and "NARROW-OK:" not in line:
                    problems.append(Problem("paths", f"{rel_s}:{no}",
                                            "path::string() narrows through the code page on Windows; use utf8() / generic_utf8()"))

        # One FNV-1a: openmui.hash.
        if not rel_s.startswith(FNV_OWN):
            for no, line in enumerate(lines, 1):
                if FNV_CONSTANTS.search(line):
                    problems.append(Problem("hash", f"{rel_s}:{no}", "a private FNV-1a; use openmui.hash (omui::hash::Fnv1a, fnv1a, fnv1a32)"))

        # One splitmix64, one fdlibm: openmui.math.
        if not rel_s.startswith("modules/base/math/"):
            for no, line in enumerate(lines, 1):
                if MATH_CONSTANTS.search(line):
                    problems.append(Problem("math", f"{rel_s}:{no}", "a private splitmix64 or fdlibm; use openmui.math (math::mix64, SplitMix64, det::)"))

        for member in DETERMINISTIC:
            if rel_s.startswith(member + "/src/"):
                for no, line in enumerate(lines, 1):
                    m = TRANSCENDENTAL.search(line)
                    if m:
                        problems.append(Problem("determinism", f"{rel_s}:{no}",
                                                f"std::{m.group(1)} differs between platforms' libm; use openmui.math's det (hanim::dm)"))
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
                if dep_rel == "modules/base/testing":
                    problems.append(Problem("layering", member, f"`{name}`: openmui.testing belongs in [dev-dependencies]"))
                    continue
                if "*" in allowed:
                    continue
                if dep_layer not in allowed:
                    problems.append(Problem("modules" if layer_of(member) in INDEPENDENT else "layering",
                                            member, f"depends on {dep_rel} ({name}); allowed layers: {sorted(allowed) or 'std only'}"))

        if member in DETERMINISTIC:
            flags = " ".join(data.get("build", {}).get("cxxflags", []))
            if "-ffp-contract=off" not in flags:
                problems.append(Problem("determinism", f"{member}/mcpp.toml", "missing -ffp-contract=off in [build] cxxflags"))
    return problems


# Where the path-is-the-name rule (D-80) holds, and the directories whose
# README lists the packages under them.
LIBRARY_ROOTS = ("modules", "mgame", "components", "lang", "tools", "showcase", "mcpp")
README_ROOTS = ("modules", "mgame", "components", "lang", "tools", "tests", "examples", "showcase", "mcpp")


def package_name(data: dict) -> str:
    p = data.get("package", {})
    return f"{p['namespace']}.{p.get('name', '')}" if p.get("namespace") else p.get("name", "")


def check_layout(root: Path) -> list[Problem]:
    """A path is its package's name; every directory with packages under it says what they are."""
    problems = []
    members = workspace_members(root)
    names: dict[str, str] = {}
    for m in members:
        try:
            names[m] = package_name(tomllib.loads((root / m / "mcpp.toml").read_text(encoding="utf-8")))
        except (OSError, tomllib.TOMLDecodeError):
            continue
    for m, full in names.items():
        if m.split("/")[0] not in LIBRARY_ROOTS:
            continue
        leaf = Path(m).name.replace("-", "_")
        if leaf != full.split(".")[-1]:
            problems.append(Problem("layout", m, f"the directory is `{Path(m).name}` but the package is {full}: "
                                                 "a library's path is its name (D-80)"))
        parent = Path(m).parent.as_posix()
        if parent in names and not full.startswith(names[parent] + "."):
            problems.append(Problem("layout", m, f"{full} lies in {parent} ({names[parent]}): a package inside another "
                                                 f"is named after it ({names[parent]}.{Path(m).name.replace('-', '_')})"))
    # Each directory with members below it lists its children in its README.
    children: dict[str, set[str]] = {}
    for m in members:
        parts = m.split("/")
        if parts[0] not in README_ROOTS:
            continue
        for i in range(1, len(parts)):
            children.setdefault("/".join(parts[:i]), set()).add(parts[i])
    for d, kids in sorted(children.items()):
        readme = root / d / "README.md"
        if not readme.exists():
            problems.append(Problem("layout", d, "a directory with packages under it has a README.md that lists them"))
            continue
        text = readme.read_text(encoding="utf-8")
        for k in sorted(kids):
            if not re.search(r"\]\(" + re.escape(k) + r"(/[^)]*)?\)", text):
                problems.append(Problem("layout", f"{d}/README.md", f"does not list `{k}` (a link `]({k})`)"))
    return problems


# Licences (D-83): GPL and AGPL never, dynamic linking included; LGPL only
# as an optional plugin linked dynamically. tools/lint/licenses.toml lists
# every index package the workspace reaches.
COPYLEFT = re.compile(r"^(A?GPL)-")
WEAK_COPYLEFT = re.compile(r"^LGPL-")


def index_packages(root: Path) -> set[str]:
    """Index packages the manifests (every table, every target) and mcpp.lock name."""
    names: set[str] = set()
    manifests = [root / m / "mcpp.toml" for m in workspace_members(root)] + [root / "mcpp.toml"]
    for manifest in manifests:
        try:
            data = tomllib.loads(manifest.read_text(encoding="utf-8"))
        except (OSError, tomllib.TOMLDecodeError):
            continue
        tables = [data.get(t, {}) for t in ("dependencies", "dev-dependencies", "build-dependencies")]
        for target in data.get("target", {}).values():
            if isinstance(target, dict):
                tables += [target.get(t, {}) for t in ("dependencies", "dev-dependencies", "build-dependencies")]
        for table in tables:
            for name, spec in flatten_deps(table):
                if isinstance(spec, str) or (isinstance(spec, dict) and "path" not in spec):
                    names.add(name)
    lock = root / "mcpp.lock"
    if lock.exists():
        try:
            names |= set(tomllib.loads(lock.read_text(encoding="utf-8")).get("package", {}).keys())
        except tomllib.TOMLDecodeError:
            pass
    return names


def index_licences(name: str) -> list[str] | None:
    """The licences a local mcpp index's descriptor lists, when one is there."""
    for pkgs in sorted(Path.home().glob(".mcpp/indices/*/pkgs")):
        candidates = list(pkgs.glob(f"*/{name}.lua"))
        if name.startswith("mcpplibs.mwrap."):
            candidates.append(pkgs / "mwrap" / (name[len("mcpplibs.mwrap."):] + ".lua"))
        if name.startswith("mcpplibs."):
            candidates += list(pkgs.glob(f"*/{name[len('mcpplibs.'):]}.lua"))
        for f in candidates:
            if f.exists():
                m = re.search(r"licenses\s*=\s*\{([^}]*)\}", f.read_text(encoding="utf-8"))
                return re.findall(r'"([^"]+)"', m.group(1)) if m else []
    return None


def check_licences(root: Path) -> list[Problem]:
    problems = []
    table_file = root / "tools" / "lint" / "licenses.toml"
    try:
        table = tomllib.loads(table_file.read_text(encoding="utf-8")) if table_file.exists() else {}
    except tomllib.TOMLDecodeError as e:
        return [Problem("licenses", "tools/lint/licenses.toml", f"not valid TOML: {e}")]
    listed: dict = table.get("packages", {})
    dynamic: dict = table.get("dynamic", {})
    for name in sorted(index_packages(root)):
        if name not in listed:
            problems.append(Problem("licenses", name, "not in tools/lint/licenses.toml: list it with its licences (SPDX) "
                                                      "before depending on it"))
            continue
        licences = listed[name]
        for lic in licences:
            if COPYLEFT.match(lic):
                problems.append(Problem("licenses", name, f"{lic}: no GPL or AGPL in openmui, dynamically linked or not (D-83)"))
            elif WEAK_COPYLEFT.match(lic) and name not in dynamic:
                problems.append(Problem("licenses", name, f"{lic}: an LGPL library only as an optional plugin, linked "
                                                          "dynamically and listed under [dynamic] (D-83)"))
        known = index_licences(name)
        if known and sorted(known) != sorted(licences):
            problems.append(Problem("licenses", name, f"tools/lint/licenses.toml says {licences}, the index says {known}"))
    return problems


# Platforms (D-84): a package that says it runs on the Web (`platforms` lists
# "emscripten") depends — dev-dependencies included, its tests run there too —
# only on packages of the workspace that say so as well. CI's Web row tests
# exactly the packages that say it (tools/ci/web.sh).
def check_platforms(root: Path) -> list[Problem]:
    problems = []
    members = workspace_members(root)
    data: dict[str, dict] = {}
    for m in members:
        try:
            data[m] = tomllib.loads((root / m / "mcpp.toml").read_text(encoding="utf-8"))
        except (OSError, tomllib.TOMLDecodeError):
            continue
    web = {m for m, d in data.items() if "emscripten" in d.get("package", {}).get("platforms", [])}
    for m in sorted(web):
        d = data[m]
        tables = [d.get(t, {}) for t in ("dependencies", "dev-dependencies")]
        for target in d.get("target", {}).values():
            if isinstance(target, dict):
                tables += [target.get(t, {}) for t in ("dependencies", "dev-dependencies")]
        for table in tables:
            for name, spec in flatten_deps(table):
                if not isinstance(spec, dict) or "path" not in spec:
                    continue
                try:
                    dep = (root / m / spec["path"]).resolve().relative_to(root.resolve()).as_posix()
                except ValueError:
                    continue
                if dep in data and dep not in web:
                    problems.append(Problem("platforms", m, f"runs on the Web (emscripten) but depends on {dep} ({name}), which does not say it does"))
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


# The data files a schema governs: the committed schema of each kind.
DOCUMENT_SCHEMAS = [
    ("*.view.omui.json", "view.schema.json", "omui schema --out spec/schema/view.schema.json"),
    ("*.scenario.omui.json", "scenario.schema.json", "spec/conformance/S/README.md describes it"),
    ("*.trace.omui.json", "scenario.schema.json", "spec/conformance/S/README.md describes it"),
]


def check_documents(root: Path) -> list[Problem]:
    try:
        import jsonschema  # type: ignore
    except ImportError:
        print("lint: documents skipped (pip install jsonschema)", file=sys.stderr)
        return []
    import json
    problems = []
    for pattern, schema_name, how in DOCUMENT_SCHEMAS:
        docs = [f for f in root.rglob(pattern) if not (set(f.relative_to(root).parts) & SKIP_DIRS)]
        if not docs:
            continue
        schema_file = root / "spec" / "schema" / schema_name
        if not schema_file.exists():
            problems.append(Problem("documents", f"spec/schema/{schema_name}", f"missing ({how})"))
            continue
        validator = jsonschema.Draft202012Validator(json.loads(schema_file.read_text(encoding="utf-8")))
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


MEDIA_LIMIT = 1024 * 1024
MEDIA_SUFFIXES = {".png", ".gif", ".jpg", ".jpeg", ".webp", ".apng"}


def check_media(root: Path) -> list[Problem]:
    problems = []
    for f in sorted(root.rglob("*")):
        rel = f.relative_to(root)
        if f.suffix.lower() not in MEDIA_SUFFIXES or set(rel.parts) & SKIP_DIRS or not f.is_file():
            continue
        if f.stat().st_size > MEDIA_LIMIT:
            problems.append(Problem("media", rel.as_posix(), f"{f.stat().st_size // 1024} KB: a picture stays under 1 MB (smaller, fewer frames)"))
    showcase = root / "showcase"
    for d in sorted(showcase.iterdir()) if showcase.is_dir() else []:
        if not (d / "mcpp.toml").exists():
            continue
        readme = d / "README.md"
        text = readme.read_text(encoding="utf-8") if readme.exists() else ""
        shown = [m for m in re.findall(r"!\[[^\]]*\]\((media/[^)\s]+)\)", text) if (d / m).exists()]
        if not shown:
            problems.append(Problem("media", f"{d.relative_to(root).as_posix()}/README.md", "shows no picture of its own from media/ (tools/ci/readme-media.sh)"))
    return problems


def run(root: Path) -> list[Problem]:
    return (check_sources(root) + check_manifests(root) + check_layout(root) + check_licences(root) + check_platforms(root) + check_pins(root)
            + check_documents(root) + check_media(root))


def self_test() -> int:
    """Each rule must reject a violation planted in a copy of a minimal tree."""
    cases = {
        "macros": ("modules/base/hanim/src/bad.cpp", "module x;\n#define BAD 1\n"),
        "headers": ("modules/ui/core/src/util.h", "int f();\n"),
        "naming": ("modules/ui/core/src/a.cpp", "namespace mui { }\n"),
        "exceptions": ("modules/base/hanim/src/e.cpp", "void f() { throw 1; }\n"),
        "paths": ("modules/ui/core/src/p.cpp", "std::string f(const std::filesystem::path& p) { return p.string(); }\n"),
        "determinism": ("modules/base/hanim/src/d.cpp", "double f(double x) { return std::sin(x); }\n"),
        "hash": ("modules/ui/core/src/h.cpp", "std::uint64_t h = 0xcbf29ce484222325ull;\n"),
        "math": ("modules/ui/core/src/m.cpp", "z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;\n"),
        "platform": ("modules/ui/core/src/native/io.cpp", "module;\n#include <unistd.h>\nmodule x;\n"),
        "layering": None,
        "modules": None,
        "layout": None,
        "licenses": None,
        "platforms": None,
        "thirdparty": ("modules/base/hanim/src/w.cpp", "module x;\nimport mcpplibs.mwrap.md4c;\n"),
        "media": ("docs/huge.png", "x" * (1024 * 1024 + 1)),
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
            members = ["modules/base/hanim", "modules/ui/core", "spec"]
            names = {"modules/base/hanim": 'name = "hanim"', "modules/ui/core": 'namespace = "openmui"\nname = "core"',
                     "spec": 'namespace = "openlifa"\nname = "openmui"'}
            if rule == "layout":
                names["modules/base/hanim"] = 'name = "anim"'
            if rule == "platforms":
                names["modules/ui/core"] += '\nplatforms = ["linux", "emscripten"]'
            pin = ""
            if rule == "pins":
                pin = ('[package]\nname = "root"\nversion = "0.1.0"\n[package.metadata.openmui.pins]\n"ns.kernel" = "ns.os"\n'
                       '[dependencies]\nns.kernel = "1.0.0"\n')
            (root / "mcpp.toml").write_text(
                pin + "[workspace]\nmembers = [" + ", ".join(f'"{m}"' for m in members) + "]\n", encoding="utf-8")
            (root / "tools" / "lint").mkdir(parents=True, exist_ok=True)
            (root / "tools" / "lint" / "licenses.toml").write_text(
                '[packages]\n"ns.kernel" = ["MIT"]\n"ns.os" = ["MIT"]\n"compat.sdl3" = ["Zlib"]\n', encoding="utf-8")
            readmes = {"modules": "[base](base) [ui](ui)", "modules/base": "[hanim](hanim)", "modules/ui": "[core](core)"}
            for d, text in readmes.items():
                (root / d).mkdir(parents=True, exist_ok=True)
                (root / d / "README.md").write_text(text + "\n", encoding="utf-8")
            for m in members:
                (root / m / "src").mkdir(parents=True, exist_ok=True)
                deps = ""
                if rule == "layering" and m == "spec":
                    deps = '[dependencies]\ncore = { path = "../modules/ui/core" }\n'
                if rule == "modules" and m == "modules/base/hanim":
                    deps = '[dependencies]\ncore = { path = "../../ui/core" }\n'
                if rule == "manifest" and m == "modules/ui/core":
                    deps = '[dependencies.compat]\nsdl3 = "3.4.18"\n'
                if rule == "pins" and m == "modules/ui/core":
                    deps = '[dependencies]\nns.os = "0.1.0"\n'  # reaches ns.os, does not pin ns.kernel
                if rule == "platforms" and m == "modules/ui/core":
                    deps = '[dependencies]\nhanim = { path = "../../base/hanim" }\n'
                if rule == "licenses" and m == "modules/ui/core":
                    deps = '[dependencies]\nacme.copyleft = "1.0.0"\n'
                    with (root / "tools" / "lint" / "licenses.toml").open("a", encoding="utf-8") as f:
                        f.write('"acme.copyleft" = ["GPL-3.0-only"]\n')
                flags = '[build]\ncxxflags = ["-ffp-contract=off"]\n' if m == "modules/base/hanim" else ""
                (root / m / "mcpp.toml").write_text(f'[package]\n{names[m]}\nversion = "0.1.0"\n{flags}{deps}',
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
