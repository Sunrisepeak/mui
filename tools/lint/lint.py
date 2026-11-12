#!/usr/bin/env python3
"""openmui's repository rules, checked (AGENTS.md; implementation plan §15.3).

    python3 tools/lint/lint.py            check the repository
    python3 tools/lint/lint.py --self-test show that each rule catches a violation

Rules:
  macros        no `#define` in our sources (spec/include/openmui.h is the one exception)
  headers       no header files but spec/include/openmui.h; `#include` only in `native/` units
  naming        no bare `mui`: openmui is the spec, omui the UI; named products, mlibs/xengine own their APIs
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
  legacy-import no import of a removed forwarder (openmui.geom, openmui.render…) and no omui::
                alias of another owner's namespace (omui::geom::…): code names the owner (D-106);
                no import of the removed Lua and Python bindings (omui.lang.lua, omui.lang.python…):
                TypeScript is the one script language (D-138)
  testing       `import mlibs.testing` only in test programs (tests/): mlibs.utils carries it, and
                no artifact does
  cmdline       a program's command line is declared with mcpplibs.cmdline, never scanned by hand
                (`== "--x"`, `argv[i]`): what a library already does is depended on (D-56, AGENTS §5)
  closure       the dependency closures CLOSURES names stay small (the render graph; simulation
                without UI, game, renderer or, for its CPU packages, GPU)
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
  owner         each capability has one owner (tools/lint/capabilities.owners.omui.json, plan
                §5.3 ①): its signature — FNV-1a, splitmix64/fdlibm, flicks, OKLab, the handle's
                shape, a component animating itself — appears nowhere else; the owner implements
                it, everyone else imports it
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
import json
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

# Layers (D-80, D-82; convergence plan §9.2). A name says who owns a package,
# a directory says which group it is in, and the layer comes from this table:
# the longest path prefix that holds a member gives its layer, so one project
# may span layers (hanim.core is base, hanim is gfx). A layer may depend on the
# layers listed for it (path dependencies in [dependencies] and target
# dependencies). The table holds today's directories and the ones M1 moves
# them to (modules/projects/…), so a move changes no rule.
#
# The independent components (mlibs, hanim, xrender, xengine's kernels below
# the UI) know nothing of omui (D-55); omui never depends on the game layer
# (xengine/game, D-161); the
# fighting framework's core depends on the standard library alone, so that it
# can leave the repository unchanged, and the rest of gongfu on no UI.
PATH_LAYERS: dict[str, str] = {
    # mlibs: the layer groups
    "modules/base/utils": "utils",  # the bottom: small tools, the standard library alone
    "modules/base": "base",
    "modules/gfx": "gfx",
    "modules/content": "content",
    "modules/media": "media",
    "modules/mpal": "base",  # the OS's services above mcpplibs.os: no UI, used by omui's hosts
    # the projects (plan §9.2)
    "modules/projects/hanim/core": "base",
    "modules/projects/hanim/timeline": "base",  # tracks of clips, their file and edits: hanim.core and mlibs alone
    "modules/projects/hanim": "gfx",
    "modules/projects/xrender": "gfx",
    "modules/projects/xrender/text": "content",  # text as the renderer draws it: above mlibs.text
    "modules/projects/xengine/kernel/dynamics": "base",  # the CPU core (hanim and omui use it, D-133)
    "modules/projects/xengine/kernel/cellular": "base",  # its CPU part
    "modules/projects/xengine/kernel/cellular/rhi": "gfx",
    "modules/projects/xengine/kernel/cellular/present": "gfx",  # cells to an xrender image
    "modules/projects/xengine/kernel/cellular/sdl": "gfx",  # the kernel's suite and field on a native device
    "modules/projects/xengine/kernel/audio": "media",
    "modules/projects/xengine/kernel/video": "media",
    "modules/projects/xengine/kernel/matter": "base",  # materials, their states and laws (D-165)
    "modules/projects/xengine/game": "game",  # above the interface: world, input, net, ui (the bridge), tool
    "modules/projects/gongfu": "gongfu",
    "modules/projects/omui": "ui",
    "modules/projects/omui/host": "host",
    "modules/projects/omui/components": "components",
    "modules/projects/omui/lang": "lang",
    "modules/projects/omui/sdk": "sdk",
    # omui and the game layer where they are today
    "modules/ui": "ui",
    "modules/projects/omui/host": "host",
    "components": "components",
    "lang": "lang",
    "modules/projects/gongfu": "gongfu",
    "mgame": "game",
    "spec": "spec",
    # build code, tools and the consumers
    "tools": "tools",
    "mcpp": "mcpp",
    "src": "app",  # entries: the composition roots (component-binding §10.2)
    "tests": "app",
    "examples": "app",
    "showcase": "app",
    "site": "app",
    ".": "app",  # the root package (until M1 moves it to modules/projects/omui/sdk)
}
# A directory that is its own layer, not the prefix of one: gongfu's core.
EXACT_LAYERS: dict[str, str] = {"modules/projects/gongfu": "gongfu.core", "modules/projects/gongfu": "gongfu.core"}
INDEPENDENT = {"utils", "base", "gfx", "content", "media"}
_BELOW_UI = {"utils", "base", "gfx", "content", "media", "spec"}
LAYERS: dict[str, set[str]] = {
    "utils": set(),
    "base": {"utils", "base"},
    "gfx": {"utils", "base", "gfx"},
    "content": {"utils", "base", "gfx", "content"},
    "media": {"utils", "base", "gfx", "content", "media"},
    "gongfu.core": set(),
    "gongfu": {"utils", "base", "gfx", "gongfu.core", "gongfu"},
    "spec": set(),
    "ui": _BELOW_UI | {"ui"},
    "host": _BELOW_UI | {"ui", "host"},
    "components": _BELOW_UI | {"ui", "components"},
    "lang": {"spec", "ui", "components", "lang"},
    "sdk": _BELOW_UI | {"ui", "host", "components", "sdk"},
    "game": _BELOW_UI | {"ui", "host", "components", "game", "gongfu.core", "gongfu"},
    "tools": {"*"},
    "mcpp": {"tools", "mcpp"},  # build code only: the plugins, their shared base and the generator they run
    "app": {"*"},
}

# Projects (D-161): the units that may each be a repository of its own one day,
# and the projects each may depend on. Test dependencies count: a project whose
# tests need a project above it could not be built alone. The layers above say
# which package may reach which; this says that no two projects need each
# other. The longest path prefix that holds a member names its project;
# consumers (tools, the site, showcases, examples, tests, entries) are free.
PROJECT_PATHS: dict[str, str] = {
    "modules": "mlibs",  # base, gfx, content, mpal
    "spec": "spec",
    "modules/projects/xrender": "xrender",
    "modules/projects/xengine/kernel/dynamics": "xengine.sim",
    "modules/projects/xengine/kernel/cellular": "xengine.sim",
    "modules/projects/xengine/kernel/matter": "xengine.sim",
    "modules/projects/hanim": "hanim",
    "modules/projects/xengine/kernel/audio": "xengine.media",
    "modules/projects/xengine/kernel/video": "xengine.media",
    "modules/projects/omui": "omui",
    "modules/projects/xengine/game": "xengine.game",
    "modules/projects/gongfu": "gongfu",
}
# gongfu's core alone (the standard library): a fixture the game layer's tests may take.
EXACT_PROJECTS: dict[str, str] = {"modules/projects/gongfu": "gongfu.core"}
_KERNELS = {"mlibs", "xrender", "xengine.sim", "hanim", "xengine.media"}
PROJECT_DEPS: dict[str, set[str]] = {
    "mlibs": set(),
    "spec": set(),
    "xrender": {"mlibs"},
    "xengine.sim": {"mlibs", "xrender"},
    "hanim": {"mlibs", "xrender", "xengine.sim"},
    "xengine.media": {"mlibs", "xrender", "xengine.sim", "hanim"},
    "omui": _KERNELS | {"spec"},
    "gongfu.core": {"mlibs"},  # for its tests; what it is built from is the standard library alone (layering)
    "gongfu": _KERNELS | {"gongfu.core"},
    "xengine.game": _KERNELS | {"spec", "omui", "gongfu.core"},
}
# In the game layer, the one package that knows omui (D-162).
OMUI_BRIDGE = "modules/projects/xengine/game/ui"

# Members whose results are part of A-level conformance (identical bits).
DETERMINISTIC = ["modules/base/math", "modules/base/utils", "modules/projects/omui/native/fuzzy", "modules/projects/xengine/kernel/audio", "modules/projects/xengine/kernel/video", "modules/projects/xengine/kernel/dynamics", "showcase/widget-breakout/sim", "modules/projects/hanim/core", "modules/projects/hanim", "modules/projects/xrender/raster", "modules/content/layout", "modules/base/geom", "modules/projects/xrender/graph", "modules/projects/xrender/vector", "modules/projects/xrender/text", "modules/content/text",
                 "modules/content/i18n", "modules/projects/omui/view", "showcase/promo-video"]

# The forwarders M1 removed (D-106): the old module names and the omui::
# namespace aliases that pointed at their owners. Code names the owner.
LEGACY_MODULES = {'openmui.raster', 'openmui.simulation.cellular.rhi.cts', 'openmui.rhi.cts', 'openmui.memory', 'openmui.geom', 'openmui.unicode', 'openmui.rhi.webgl', 'openmui.image', 'openmui.testing', 'openmui.render.vector', 'openmui.pipeline', 'openmui.rhi', 'openmui.simulation.cellular', 'openmui.trace', 'openmui.shader', 'openmui.render', 'openmui.shader.rules', 'openmui.rhi.sdl', 'openmui.rhi.webgpu', 'openmui.log', 'openmui.markdown', 'openmui.i18n.cldr', 'openmui.layout', 'openmui.session', 'openmui.text', 'openmui.math', 'openmui.hash', 'openmui.render.effect', 'openmui.i18n', 'openmui.simulation.cellular.rhi', 'openmui.simulation'}
# The script bindings D-138 removed: TypeScript (omui.lang.js) is the one script language.
REMOVED_BINDINGS = re.compile(r"^omui\.(lang\.lua|lang\.python|entry\.python)(\.|:|$)")
# A command line scanned by hand: an argument compared with a `--option`, or argv indexed by a variable.
HAND_PARSED = re.compile(r'==\s*"--[A-Za-z]|starts_with\("--[A-Za-z]|\bargv\[[a-z_]\w*\]')
LEGACY_ALIASES = re.compile(r"(?<![\w])omui::(cellular|effect|geom|hash|i18n|imageio|layout|math|md|pipeline|raster|render|rhi|session|shader|testing|trace|typeset|unicode)::")

# Operating-system headers: platform code goes through mcpplibs.os (D-57), so
# none of ours includes them, native units included.
OS_HEADERS = re.compile(
    r"#\s*include\s*[<\"](windows\.h|winsock2?\.h|unistd\.h|pthread\.h|dlfcn\.h|fcntl\.h|sys/[^>\"]+|mach/[^>\"]+|"
    r"CoreFoundation/[^>\"]+|Cocoa/[^>\"]+|AppKit/[^>\"]+|linux/[^>\"]+)[>\"]")
# Each capability's one owner, and the signature of a re-implementation.
OWNERS_FILE = Path(__file__).resolve().parent / "capabilities.owners.omui.json"


def load_owners() -> list[dict]:
    rows = json.loads(OWNERS_FILE.read_text(encoding="utf-8"))["capability"]
    for row in rows:
        flags = re.IGNORECASE if row.get("ignore_case") else 0
        row["compiled"] = [re.compile(x, flags) for x in ([row["pattern"]] if "pattern" in row else row["together"])]
        row["mine"] = tuple(row.get("paths", [])) + tuple(a["path"] for a in row.get("allow", []))
    return rows


OWNERS = load_owners()
TRANSCENDENTAL = re.compile(
    r"\bstd::(exp|exp2|expm1|log|log2|log10|log1p|pow|sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|cbrt|hypot|lerp)\s*\(")


@dataclass
class Problem:
    rule: str
    where: str
    message: str

    def __str__(self) -> str:
        return f"[{self.rule}] {self.where}: {self.message}"


def layer_of(member: str) -> str | None:
    """The layer of a member's directory: its own entry, else the longest path prefix in the table."""
    if member in EXACT_LAYERS:
        return EXACT_LAYERS[member]
    best = None
    for prefix in PATH_LAYERS:
        if prefix == "." or member == prefix or member.startswith(prefix + "/"):
            if best is None or best == "." or len(prefix) > len(best):
                best = prefix
    return PATH_LAYERS[best] if best is not None else None


def rule_for(member: str) -> set[str] | None:
    layer = layer_of(member)
    return LAYERS.get(layer) if layer else None


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
        elif c == "'" and not (i > 0 and text[i - 1].isalnum()):
            # A character literal ('"', '\''); after a digit, a digit separator (1'000).
            j = i + 1
            while j < n and text[j] != "'" and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append("''")
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
            mi = re.match(r"(export\s+)?import\s+([\w.]+)\s*;", stripped)
            mo = re.match(r"(export\s+)?(?:module|import)\s+(openmui(?:[.:][\w.:]*)?)\s*;", stripped)
            if mo and not mo.group(2).startswith("openmui.spec") and not rel_s.startswith("spec/"):
                problems.append(Problem("legacy-import", f"{rel_s}:{no}", f"`{mo.group(2)}`: omui's modules are omui.* (D-107); openmui is the specification's"))
            if mi and mi.group(2) in LEGACY_MODULES:
                problems.append(Problem("legacy-import", f"{rel_s}:{no}", f"`{mi.group(2)}` was a forwarder (D-106): import its owner"))
            if mi and REMOVED_BINDINGS.match(mi.group(2)):
                problems.append(Problem("legacy-import", f"{rel_s}:{no}", f"`{mi.group(2)}`: the Lua and Python bindings are gone (D-138); write it in TypeScript"))
            if LEGACY_ALIASES.search(line):
                problems.append(Problem("legacy-import", f"{rel_s}:{no}", "an omui:: alias of another owner's namespace (D-106): name the owner"))
            if (re.match(r"(export\s+)?import\s+(mlibs|openmui)\.testing\b", stripped) and "tests" not in rel.parts
                    and not rel_s.startswith("modules/base/utils/src/testing/")):
                problems.append(Problem("testing", f"{rel_s}:{no}", "mlibs.testing is for test programs (tests/): no artifact carries it"))
            # Test programs (a package's tests/) are mtest's; the benchmark runners under tests/bench are programs.
            if HAND_PARSED.search(line) and ("tests" not in rel.parts or rel_s.startswith("tests/bench/")):
                problems.append(Problem("cmdline", f"{rel_s}:{no}", "a command line scanned by hand: declare its options with mcpplibs.cmdline "
                                        "(a program run by omui::run adds them beside RunOptions::declare) (D-56)"))
            if re.search(r"\bmui(::|_[a-z])|\bMUI_|\bnamespace\s+mui\b|\bmodule\s+mui\b|import\s+mui\b", line):
                problems.append(Problem("naming", f"{rel_s}:{no}", "bare `mui`; use the owner: openmui (spec), omui (UI), a named product, mlibs or xengine"))

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

        # One owner a capability: its signature nowhere else (plan §5.3 ①).
        raw = text.splitlines()
        for cap in OWNERS:
            if rel_s.startswith(cap["mine"]) or ("within" in cap and not rel_s.startswith(tuple(cap["within"]))):
                continue
            if any(part in rel.parts for part in cap.get("skip", [])):
                continue
            exprs, window = cap["compiled"], cap.get("window", 1)
            for no in range(1, len(lines) + 1):
                if "marker" in cap and cap["marker"] in raw[no - 1]:
                    continue
                if len(exprs) == 1:
                    hit = bool(exprs[0].search(lines[no - 1]))
                else:
                    span = lines[no - 1:no - 1 + window]
                    hit = bool(exprs[0].search(lines[no - 1])) and all(any(x.search(l) for l in span) for x in exprs[1:])
                if hit:
                    problems.append(Problem("owner", f"{rel_s}:{no}", f"{cap['name']}: {cap['owner']} has it; use {cap['use']}"))

        for member in DETERMINISTIC:
            if rel_s.startswith(member + "/src/"):
                for no, line in enumerate(lines, 1):
                    m = TRANSCENDENTAL.search(line)
                    if m:
                        problems.append(Problem("determinism", f"{rel_s}:{no}",
                                                f"std::{m.group(1)} differs between platforms' libm; use mlibs.math's det (hanim::dm)"))
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
        if rel.startswith("modules/projects/omui/templates/"):
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
                dep_layer = layer_of(dep_rel if dep_rel != "" else ".")
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


def project_of(member: str) -> str | None:
    """The project of a member's directory (None: a consumer, free to depend on any)."""
    if member in EXACT_PROJECTS:
        return EXACT_PROJECTS[member]
    best = None
    for prefix in PROJECT_PATHS:
        if member == prefix or member.startswith(prefix + "/"):
            if best is None or len(prefix) > len(best):
                best = prefix
    return PROJECT_PATHS[best] if best is not None else None


def check_projects(root: Path) -> list[Problem]:
    """No project depends on one that is not below it, its tests included (D-161)."""
    problems = []
    for member in workspace_members(root):
        own = project_of(member)
        if own is None:
            continue
        try:
            data = tomllib.loads((root / member / "mcpp.toml").read_text(encoding="utf-8"))
        except (OSError, tomllib.TOMLDecodeError):
            continue
        tables = [(data.get("dependencies", {}), ""), (data.get("dev-dependencies", {}), " for its tests")]
        tables += [(t, "") for t in data.get("feature-deps", {}).values()]
        for target in data.get("target", {}).values():
            if isinstance(target, dict):
                tables += [(target.get("dependencies", {}), ""), (target.get("dev-dependencies", {}), " for its tests")]
        for table, what in tables:
            for name, spec in flatten_deps(table):
                if not isinstance(spec, dict) or "path" not in spec:
                    continue
                try:
                    dep = (root / member / spec["path"]).resolve().relative_to(root.resolve()).as_posix()
                except ValueError:
                    continue
                other = project_of(dep)
                if other is None:
                    problems.append(Problem("project-dag", member, f"depends on {dep} ({name}){what}: a consumer, which no project depends on"))
                elif other != own and other not in PROJECT_DEPS.get(own, set()):
                    problems.append(Problem("project-dag", member, f"depends on {dep} ({name}){what}: {own} may depend on "
                                                                   f"{sorted(PROJECT_DEPS.get(own, set())) or 'no project'}, not on {other}"))
                elif other == "omui" and own == "xengine.game" and member != OMUI_BRIDGE:
                    problems.append(Problem("project-dag", member, f"depends on {dep} ({name}){what}: in the game layer only "
                                                                   f"{OMUI_BRIDGE} knows omui (D-162)"))
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
    for t in sorted((root / "modules/projects/omui/templates").glob("*/mcpp.toml.in")) if (root / "modules/projects/omui/templates").exists() else []:
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


# Dependency closures that stay small (D-55; plan §5.3 ⑤), by package
# identity: a package's [dependencies], its target dependencies and the
# feature-deps of the features it is built with (its defaults and what each
# edge asks for), followed through path dependencies. `only`: nothing else may
# be reached; `not`: none of these (a name, or a name and the packages below it).
CLOSURES: dict[str, dict[str, set[str]]] = {
    # the render graph: devices, handles and the small tools, nothing more
    "xrender.graph": {"only": {"xrender.graph", "mlibs.rhi", "mlibs.memory", "mlibs.utils"}},
    # drawing vectors needs no effect loader and no shader compiler
    "xrender.vector": {"not": {"xrender.effect", "mlibs.shader"}},
    # text lays out and gives glyphs as geometry; xrender.text draws them
    "mlibs.text": {"not": {"xrender"}},
    # the stepping substrate: the standard library and the small tools only,
    # no solver (D-133)
    "mlibs.simulation": {"only": {"mlibs.simulation", "mlibs.utils"}},
    # simulation knows no UI, game or renderer; its CPU packages no GPU or
    # compiler; the cellular rules do not make the clock and history mandatory
    # every solved simulation: no UI, game, renderer, GPU or compiler in its CPU core (D-133)
    "xengine.dynamics": {"not": {"omui", "xengine.world", "xengine.input", "xengine.net", "xengine.ui", "xrender", "mlibs.rhi", "mlibs.shader", "xengine.cellular"}},
    "xengine.cellular": {"not": {"omui", "xengine.world", "xengine.input", "xengine.net", "xengine.ui", "xrender", "mlibs.rhi", "mlibs.shader"}},
    "xengine.cellular.rhi": {"not": {"omui", "xengine.world", "xengine.input", "xengine.net", "xengine.ui", "xrender"}},
    # the field on screen (its tests draw it through the frame's entry, read by an agent): the renderer's resources
    # (and what they reach), no UI, game, animation, effect loader or shader compiler
    "xengine.cellular.present": {"not": {"omui", "xengine.world", "xengine.input", "xengine.net", "xengine.ui", "xengine.runtime", "hanim", "mlibs.shader", "xrender.effect"}},
    # the agent kernel: below every engine, knowing none of them (D-173)
    "mlibs.agent": {"not": {"omui", "xengine", "hanim", "xrender", "gongfu"}},
    # a game on its own: no interface, no bridge to one (D-162)
    "xengine.runtime": {"not": {"omui", "xengine.ui"}},
    # the frame's entry: the renderer and the agent kernel, no interface, game or animation (D-162; the plan's T0.6)
    "xrender.frame": {"not": {"omui", "xengine", "hanim"}},
    # the breakout on its own: its simulation and the runtime, nothing of omui's interface
    "omui.showcase.widget_breakout.headless": {"not": {"omui.core", "omui.view", "omui.semantic", "omui.agent", "omui.viewdoc", "omui.player", "omui.designer",
                                                        "omui.sdk", "omui.host", "omui.ui", "omui.theme", "omui.lang", "xengine.ui"}},
}


def _features(data: dict, asked: set[str], defaults: bool) -> set[str]:
    table = data.get("features", {})
    pending = set(asked) | (set(table.get("default", [])) if defaults else set())
    active: set[str] = set()
    while pending:
        f = pending.pop()
        if f in active or "/" in f:
            continue
        active.add(f)
        spec = table.get(f, [])
        pending |= set(spec if isinstance(spec, list) else spec.get("implies", []))
    return active


def closure_of(root: Path, member: str) -> set[str]:
    """The identities a member reaches through path dependencies (see CLOSURES)."""
    seen: set[tuple[str, frozenset[str], bool]] = set()
    names: set[str] = set()
    pending = [(root / member, frozenset(), True)]
    while pending:
        directory, asked, defaults = pending.pop()
        key = (directory.resolve().as_posix(), asked, defaults)
        if key in seen:
            continue
        seen.add(key)
        data = tomllib.loads((directory / "mcpp.toml").read_text(encoding="utf-8"))
        names.add(package_name(data))
        tables = [data.get("dependencies", {})]
        tables += [t.get("dependencies", {}) for t in data.get("target", {}).values() if isinstance(t, dict)]
        tables += [data.get("feature-deps", {}).get(f, {}) for f in _features(data, set(asked), defaults)]
        for table in tables:
            for _name, spec in flatten_deps(table):
                if isinstance(spec, dict) and "path" in spec and (directory / spec["path"] / "mcpp.toml").is_file():
                    pending.append((directory / spec["path"], frozenset(spec.get("features", [])), spec.get("default-features", True)))
    return names


def check_closures(root: Path) -> list[Problem]:
    problems = []
    by_name = {}
    for m in workspace_members(root):
        try:
            by_name[package_name(tomllib.loads((root / m / "mcpp.toml").read_text(encoding="utf-8")))] = m
        except (OSError, tomllib.TOMLDecodeError):
            continue
    if not set(CLOSURES) & set(by_name):
        return problems  # a tree without these packages (the self-test's)
    for name, rule in CLOSURES.items():
        if name not in by_name:
            problems.append(Problem("closure", name, "a package the closure rules name is not a member; update CLOSURES"))
            continue
        reached = closure_of(root, by_name[name]) - {name}
        extra = sorted(reached - rule["only"]) if "only" in rule else \
            sorted(n for n in reached if any(n == p or n.startswith(p + ".") for p in rule["not"]))
        for n in extra:
            problems.append(Problem("closure", by_name[name], f"reaches {n}, which its closure must not (tools/lint CLOSURES)"))
    return problems


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


OPERATION = re.compile(r'"([a-z0-9_-]+\.[a-z0-9._-]+)"\s*,\s*openas::Effect::')
TS_OPERATION_NAME = re.compile(r'\bname:\s*"([a-z0-9_-]+\.[a-z0-9._-]+)"')


def check_operations(root: Path) -> list[Problem]:
    """A package that declares operations (a name with an openas effect) has
    catalogs, <product>.ops.omui.toml or <product>.ops.omui.ts (defineOps,
    D-139), naming each one, and a skill.omui.md (convergence plan §10.3);
    omui-gen ops makes its table from the catalog."""
    import tomllib
    problems = []
    for member in workspace_members(root):
        base = root / member
        declared = {}
        for f in source_files(base):
            rel = f.relative_to(base)
            if "tests" in rel.parts or "generated" in rel.parts:
                continue
            for m in OPERATION.finditer(f.read_text(encoding="utf-8")):  # names are string literals: not stripped
                declared.setdefault(m.group(1), f.relative_to(root).as_posix())
        catalogs = sorted([*base.glob("*.ops.omui.toml"), *base.glob("*.ops.omui.ts")])
        if not declared and not catalogs:
            continue
        where = f"{member}/"
        if not catalogs:
            problems.append(Problem("ops", where, f"declares operations ({', '.join(sorted(declared)[:3])}…) but has no catalog: a <product>.ops.omui.toml or .ops.omui.ts"))
            continue
        # A package may hold more than one product's catalog (omui.agent: the protocol's and Live Design's).
        listed = set()
        for catalog in catalogs:
            if catalog.suffix == ".ts":
                # Operation names are dotted string literals (`name: "document.check"`); parameters' are not.
                listed |= set(TS_OPERATION_NAME.findall(catalog.read_text(encoding="utf-8")))
                continue
            try:
                data = tomllib.loads(catalog.read_text(encoding="utf-8"))
            except tomllib.TOMLDecodeError as e:
                problems.append(Problem("ops", catalog.relative_to(root).as_posix(), str(e)))
                continue
            listed |= {op.get("name") for op in data.get("operation", [])}
        names = " or ".join(catalog.name for catalog in catalogs)
        for name, f in sorted(declared.items()):
            if name not in listed:
                problems.append(Problem("ops", f, f"operation `{name}` is not in {names}"))
        if not (base / "skill.omui.md").exists():
            problems.append(Problem("ops", where, "has operations but no skill.omui.md (how an Agent does the work, and how it is verified)"))
    return problems


SKILL_NAME = re.compile(r'`([a-z][a-z0-9_-]*(?:\.[a-z0-9_-]+)+)`')
FILE_NAME = re.compile(r'\.(ts|json|jsonl|md|toml|png|webm|webp|cpp|cppm|slang)$')


def check_skills(root: Path) -> list[Problem]:
    """A skill is an SOP over operations its catalogs describe (D-145: the API
    is exported from the contracts, a skill keeps only the procedure): every
    operation it names (`design.set`, a dotted name whose product has a
    catalog) is in a catalog, so a renamed or removed operation cannot linger
    in a skill; and it points at its own package's catalog."""
    problems = []
    operations = set()
    catalogs_of = {}
    for catalog in root.rglob("*.ops.omui.ts"):
        rel = catalog.relative_to(root).as_posix()
        if "target/" in rel:
            continue
        names = set(TS_OPERATION_NAME.findall(catalog.read_text(encoding="utf-8")))
        operations |= names
        catalogs_of.setdefault(catalog.parent, []).append(catalog.name)
    products = {name.split(".")[0] for name in operations}
    for skill in root.rglob("skill.omui.md"):
        rel = skill.relative_to(root).as_posix()
        if "target/" in rel:
            continue
        text = skill.read_text(encoding="utf-8")
        for number, line in enumerate(text.splitlines(), 1):
            for m in SKILL_NAME.finditer(line):
                name = m.group(1)
                if name.split(".")[0] in products and name not in operations and not FILE_NAME.search(name):
                    problems.append(Problem("skills", f"{rel}:{number}", f"names `{name}`, which no operation catalog has"))
        own = catalogs_of.get(skill.parent, [])
        if own and not any(catalog in text for catalog in own):
            problems.append(Problem("skills", rel, f"does not point at its catalog ({', '.join(own)}): the operations are described there, not here"))
    return problems


# The agent surface (D-174): every package of a project says how an agent
# reaches what it holds, in its manifest — [package.metadata.openmui] `agent`:
#   "subject"                it serves a subject of the agent kernel: its own catalog (<product>.ops.omui.ts), a
#                            README section "Agent surface", and a test that runs the contract suite on it
#                            (mlibs.agent.cts)
#   "tool"                   a command line an agent runs: its catalog (and, by rule `ops`, its skill)
#   { part = "<package>" }   what it holds is reached through that package's subject
#   { library = "<why>" }    it holds nothing an agent could reach, and says why
# A new package declares before its code is reviewed. What existed before the
# rule and still owes its surface is listed here with what brings it; a
# package listed may not also declare, so the list only shrinks.
AGENT_SURFACE_OWED: dict[str, str] = {
    "modules/projects/hanim": "T0.8: the stage as a subject (`animation.*` through the agent kernel) under the contract suite",
    "modules/projects/xengine/kernel/audio": "M6.4: what is sounding, each bus's loudness, a sound back to its event (the plan's §3.4: a structured way to listen)",
    "modules/projects/xengine/game/world": "M6.2: entities and scenes as the game subject's addresses and observation by region and kind (§3.4)",
    "modules/projects/xengine/game/net": "M6.2: a match's rollbacks as branches, a desync located to its step and channel (§3.4)",
    "modules/projects/gongfu": "after M6.2: a match on xengine.runtime as a subject (its studio's tool is declared)",
}
AGENT_SURFACE_HEADING = re.compile(r"^#{2,3} Agent surface\b", re.MULTILINE)
AGENT_CONTRACT_SUITE = re.compile(r"^\s*import\s+mlibs\.agent\.cts\s*;", re.MULTILINE)


def check_agent_surface(root: Path) -> list[Problem]:
    import tomllib
    problems = []
    declared: dict[str, tuple[str, object]] = {}  # package name → (member, its declaration)
    members = [m for m in workspace_members(root) if m.startswith("modules/projects/")]
    for member in members:
        try:
            data = tomllib.loads((root / member / "mcpp.toml").read_text(encoding="utf-8"))
        except (OSError, tomllib.TOMLDecodeError):
            continue  # rule `manifest` says so
        declared[package_name(data)] = (member, data.get("package", {}).get("metadata", {}).get("openmui", {}).get("agent"))
    owed_packages = {name for name, (member, _) in declared.items() if member in AGENT_SURFACE_OWED}
    subjects = {name for name, (_, said) in declared.items() if said == "subject"}
    for owed in sorted(set(AGENT_SURFACE_OWED) - set(members)):
        problems.append(Problem("agent-surface", "tools/lint/lint.py", f"AGENT_SURFACE_OWED lists {owed}, which is no member under modules/projects/"))
    for name, (member, said) in sorted(declared.items(), key=lambda item: item[1][0]):
        where = f"{member}/mcpp.toml"
        base = root / member
        if member in AGENT_SURFACE_OWED:
            if said is not None:
                problems.append(Problem("agent-surface", where, "declares its agent surface and is still in AGENT_SURFACE_OWED: take it off the list"))
            continue
        if said is None:
            problems.append(Problem("agent-surface", where, 'says nothing of its agent surface: [package.metadata.openmui] agent = "subject", "tool", '
                                                            '{ part = "<package>" } or { library = "<why>" } (D-174: the surface is designed first)'))
        elif said == "subject":
            if not list(base.glob("*.ops.omui.ts")):
                problems.append(Problem("agent-surface", where, "is a subject without a catalog of its own (<product>.ops.omui.ts)"))
            readme = base / "README.md"
            if not readme.exists() or not AGENT_SURFACE_HEADING.search(readme.read_text(encoding="utf-8")):
                problems.append(Problem("agent-surface", f"{member}/README.md", 'is a subject\'s and has no "Agent surface" section (subject and addresses, observation, '
                                                                                  "operations and effects, causes, preview and time, budgets and tiers, what it cannot do)"))
            tests = base / "tests"
            if not tests.is_dir() or not any(AGENT_CONTRACT_SUITE.search(f.read_text(encoding="utf-8")) for f in tests.glob("*.cpp")):
                problems.append(Problem("agent-surface", where, "is a subject no test runs the contract suite on (import mlibs.agent.cts; contract::run(probe))"))
        elif said == "tool":
            if not list(base.glob("*.ops.omui.ts")):
                problems.append(Problem("agent-surface", where, "is a tool without a catalog (<product>.ops.omui.ts)"))
        elif isinstance(said, dict) and set(said) == {"part"} and isinstance(said["part"], str):
            if said["part"] == name or (said["part"] not in subjects and said["part"] not in owed_packages):
                problems.append(Problem("agent-surface", where, f"is part of `{said['part']}`, which is no subject among the projects' packages (nor one still owed)"))
        elif isinstance(said, dict) and set(said) == {"library"} and isinstance(said["library"], str):
            if len(said["library"].split()) < 3:
                problems.append(Problem("agent-surface", where, "is a library and does not say why it holds nothing an agent could reach"))
        else:
            problems.append(Problem("agent-surface", where, f'`agent = {said!r}` is none of "subject", "tool", {{ part = "<package>" }}, {{ library = "<why>" }}'))
    return problems


LEAD_FEATURES = ".agents/docs/evaluation/lead.features.omui.json"


def check_lead(root: Path) -> list[Problem]:
    """The signature features (Part 2 plan §1.5, D-135): two to four headline
    features a project; one with evidence names a signature-scene test that
    exists; one claimed far ahead has a far_ahead record in the lead ledger;
    the metric list well formed. The rules live in tools/eval/lead.py."""
    if not (root / LEAD_FEATURES).exists():
        return []
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "eval"))
    import lead  # type: ignore
    return [Problem("lead", where, what) for where, what in lead.problems(root)]


def check_typescript_data(root: Path) -> list[Problem]:
    """omui's data files are TypeScript data modules (D-138, D-139:
    `<name>.<kind>.omui.ts`, defineContract … defineBehavior) or their JSON
    snapshots (D-159: the spec's registry, test data): no `*.omui.toml` is
    read any more (W1 step 3). tools/dev/toml_to_ts.py converts one."""
    problems = []
    for path in root.rglob("*.omui.toml"):
        rel = path.relative_to(root).as_posix()
        if "target/" in rel or "/." in "/" + rel:
            continue
        problems.append(Problem("typescript", rel, f"is TOML: write it as {path.name[:-len('.toml')]}.ts (tools/dev/toml_to_ts.py converts it)"))
    return problems


def run(root: Path) -> list[Problem]:
    return (check_sources(root) + check_manifests(root) + check_projects(root) + check_layout(root) + check_licences(root) + check_platforms(root) + check_pins(root)
            + check_documents(root) + check_media(root) + check_closures(root) + check_operations(root) + check_skills(root) + check_agent_surface(root)
            + check_lead(root) + check_typescript_data(root))


def self_test() -> int:
    """Each rule must reject a violation planted in a copy of a minimal tree."""
    cases = {
        "macros": ("modules/base/hanim/core/src/bad.cpp", "module x;\n#define BAD 1\n"),
        "headers": ("modules/ui/core/src/util.h", "int f();\n"),
        "naming": ("modules/ui/core/src/a.cpp", "namespace mui { }\n"),
        "exceptions": ("modules/base/hanim/core/src/e.cpp", "void f() { throw 1; }\n"),
        "paths": ("modules/ui/core/src/p.cpp", "std::string f(const std::filesystem::path& p) { return p.string(); }\n"),
        "determinism": ("modules/base/math/src/d.cpp", "double f(double x) { return std::sin(x); }\n"),
        "platform": ("modules/ui/core/src/native/io.cpp", "module;\n#include <unistd.h>\nmodule x;\n"),
        "layering": None,
        "modules": None,
        "layout": None,
        "licenses": None,
        "platforms": None,
        "testing": ("modules/ui/core/src/t.cpp", "module x;\nimport mlibs.testing;\n"),
        "cmdline": ("tools/x/src/main.cpp", "int main(int argc, char** argv) {\n    for (int i = 1; i < argc; ++i) if (std::string_view(argv[i]) == \"--json\") return 1;\n}\n"),
        "legacy-import": ("modules/ui/core/src/l.cpp", "module x;\nimport openmui.geom;\n"),
        "thirdparty": ("modules/base/hanim/core/src/w.cpp", "module x;\nimport mcpplibs.mwrap.md4c;\n"),
        "media": ("docs/huge.png", "x" * (1024 * 1024 + 1)),
        "manifest": None,
        "project-dag": None,
        "agent-surface": None,
        "pins": None,
        "closure": None,
        "typescript": ("modules/ui/core/contract/x.contract.omui.toml", "[contract.x]\nrole = \"button\"\n"),
        "ops": ("modules/ui/core/src/o.cpp", "module x;\nconstexpr auto op = mlibs::control::Operation{\"ui.wipe\", openas::Effect::irreversible};\n"),
        "documents": ("examples/x/ui/bad.view.omui.json", '{"omui": "0.1", "view": {"root": {"typ": "text"}}}'),
        "skills": ("modules/ui/core/skill.omui.md", "# x\n\nSee [x.ops.omui.ts](x.ops.omui.ts); then `ui.wipe`.\n"),
        "lead": (LEAD_FEATURES, '{"feature": [{"id": "A", "project": "omui", "line": "use-safely", "title": "x", "status": "claimed"}]}\n'),
    }
    try:
        import jsonschema  # type: ignore  # noqa: F401
    except ImportError:
        del cases["documents"]
        print("  skip  documents: pip install jsonschema")
    for cap in OWNERS:
        cases[f"owner:{cap['name']}"] = (cap["sample"]["path"], cap["sample"]["text"] + "\n")
    failures = 0
    for rule, planted in cases.items():
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            members = ["modules/gfx/hanim", "modules/base/hanim/core", "modules/ui/core", "spec"]
            names = {"modules/gfx/hanim": 'name = "hanim"', "modules/base/hanim/core": 'namespace = "hanim"\nname = "core"', "modules/ui/core": 'namespace = "openmui"\nname = "core"',
                     "spec": 'namespace = "openlifa"\nname = "openmui"'}
            if rule == "layout":
                names["modules/gfx/hanim"] = 'name = "anim"'
            if rule == "agent-surface":  # a project's package that says nothing of its agent surface
                members.append("modules/projects/thing")
                names["modules/projects/thing"] = 'name = "thing"'
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
            readmes = {"modules": "[base](base) [gfx](gfx) [ui](ui)", "modules/base": "[hanim](hanim)", "modules/base/hanim": "[core](core)", "modules/gfx": "[hanim](hanim)", "modules/ui": "[core](core)"}
            if rule == "agent-surface":
                readmes["modules"] += " [projects](projects)"
                readmes["modules/projects"] = "[thing](thing)"
            for d, text in readmes.items():
                (root / d).mkdir(parents=True, exist_ok=True)
                (root / d / "README.md").write_text(text + "\n", encoding="utf-8")
            for m in members:
                (root / m / "src").mkdir(parents=True, exist_ok=True)
                deps = ""
                if rule == "layering" and m == "spec":
                    deps = '[dependencies]\ncore = { path = "../modules/ui/core" }\n'
                if rule == "modules" and m == "modules/gfx/hanim":
                    deps = '[dependencies]\ncore = { path = "../../ui/core" }\n'
                if rule == "closure" and m == "modules/gfx/hanim":
                    deps = '[dependencies]\nhanim.core = { path = "../../base/hanim/core" }\n'
                if rule == "project-dag" and m == "modules/base/hanim/core":  # mlibs, with a test dependency on the specification
                    deps = '[dev-dependencies]\nopenlifa.openmui = { path = "../../../../spec" }\n'
                if rule == "manifest" and m == "modules/ui/core":
                    deps = '[dependencies.compat]\nsdl3 = "3.4.18"\n'
                if rule == "pins" and m == "modules/ui/core":
                    deps = '[dependencies]\nns.os = "0.1.0"\n'  # reaches ns.os, does not pin ns.kernel
                if rule == "platforms" and m == "modules/ui/core":
                    deps = '[dependencies]\nhanim = { path = "../../gfx/hanim" }\n'
                if rule == "licenses" and m == "modules/ui/core":
                    deps = '[dependencies]\nacme.copyleft = "1.0.0"\n'
                    with (root / "tools" / "lint" / "licenses.toml").open("a", encoding="utf-8") as f:
                        f.write('"acme.copyleft" = ["GPL-3.0-only"]\n')
                flags = '[build]\ncxxflags = ["-ffp-contract=off"]\n' if m in ("modules/gfx/hanim", "modules/base/hanim/core") else ""
                (root / m / "mcpp.toml").write_text(f'[package]\n{names[m]}\nversion = "0.1.0"\n{flags}{deps}',
                                                    encoding="utf-8")
            if rule == "documents":
                (root / "spec" / "schema").mkdir(parents=True, exist_ok=True)
                shutil.copy(Path(__file__).resolve().parents[2] / "spec" / "schema" / "view.schema.json",
                            root / "spec" / "schema" / "view.schema.json")
            if rule == "skills":  # the catalog its skill reads beside it, without the operation the skill names
                (root / "modules/ui/core/x.ops.omui.ts").write_text('export default defineOps({ operations: [{ name: "ui.snapshot" }] });\n', encoding="utf-8")
            if planted:
                path, text = planted
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_text(text, encoding="utf-8")
            global CLOSURES, AGENT_SURFACE_OWED
            saved, saved_owed = CLOSURES, AGENT_SURFACE_OWED
            AGENT_SURFACE_OWED = {}  # the list is this repository's: the planted tree has none of its packages
            if rule == "closure":
                CLOSURES = {"hanim": {"not": {"hanim.core"}}}  # planted: hanim may not reach its core
            kind, _, capability = rule.partition(":")
            found = [p for p in run(root) if p.rule == kind and (not capability or p.message.startswith(capability + ":"))]
            # A specification that depends on a library (the layering case) breaks the projects' order too.
            clean = [p for p in run(root) if p.rule != kind and not (rule == "layering" and p.rule == "project-dag")]
            CLOSURES, AGENT_SURFACE_OWED = saved, saved_owed
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
