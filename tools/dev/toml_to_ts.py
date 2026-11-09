#!/usr/bin/env python3
"""TOML data files to TypeScript data modules (W1 step 3, D-138/D-139).

  tools/dev/toml_to_ts.py FILE…          write <name>.<kind>.omui.ts beside each, print what it wrote
  tools/dev/toml_to_ts.py --check FILE…  write nothing; say what each would become

A contract (`[contract.<name>]`), a native module (`[native.<name>]`) or an
operation catalog (`[catalog]`, `[[operation]]`, `[[tier]]`) becomes the
module omui-gen reads the same document from (omui.gen.data):

  defineContract({ <name>: {…} })      one component a file, named as the file
  defineNative({ <name>: {…} })
  defineOps({ product, doc, …, operations: […], tiers: […] })
  defineTheme({ <name>: {…} }), defineStyle({ <name>: {…} })   (the reader in omui.theme)

The file's leading comment stays, as `//` lines (a theme's or a style's every
comment, where it was). Tables of scalars print on
one line where they fit (props, args, params), as the TOML had them inline;
the rest nests. Values keep their TOML types: TOML integers and floats both
become JS numbers, which omui.gen.data reads back as the integer or float
JSON gives. The TOML file is not removed: the caller checks the generated
outputs are byte for byte the same first (gen_test, omui-gen ops --check,
the contracts plugin's partitions), then deletes it.
"""
from __future__ import annotations

import json
import re
import sys
import tomllib
from pathlib import Path

IDENT = re.compile(r"^[A-Za-z_$][A-Za-z0-9_$]*$")
HEADER = re.compile(r"^\s*\[\s*([^\]]+?)\s*\]\s*(#.*)?$")
KEY = re.compile(r"^\s*([A-Za-z0-9_\-]+|\"[^\"]+\")\s*=")
WIDTH = 140


def key(name: str) -> str:
    return name if IDENT.match(name) else json.dumps(name, ensure_ascii=False)


def scalar(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float) and value.is_integer():
        return repr(value)  # 1.0 stays a float's spelling
    return json.dumps(value, ensure_ascii=False)


def inline(value) -> str:
    if isinstance(value, dict):
        if not value:
            return "{}"
        return "{ " + ", ".join(f"{key(k)}: {inline(v)}" for k, v in value.items()) + " }"
    if isinstance(value, list):
        return "[" + ", ".join(inline(v) for v in value) + "]"
    return scalar(value)


def block(value, indent: int) -> str:
    """A value at `indent`, on one line when it fits, else nested."""
    pad = "  " * indent
    one = inline(value)
    if len(pad) + len(one) <= WIDTH or not isinstance(value, (dict, list)) or not value:
        return one
    inner = "  " * (indent + 1)
    if isinstance(value, dict):
        lines = [f"{inner}{key(k)}: {block(v, indent + 1)}," for k, v in value.items()]
        return "{\n" + "\n".join(lines) + f"\n{pad}}}"
    lines = [f"{inner}{block(v, indent + 1)}," for v in value]
    return "[\n" + "\n".join(lines) + f"\n{pad}]"


def header(text: str) -> list[str]:
    out = []
    for line in text.splitlines():
        if not line.startswith("#"):
            break
        rest = line[1:]
        out.append("//" + (" " + rest.lstrip() if rest.strip() else ""))
    return out


def path_of(name: str) -> tuple[str, ...]:
    parts, word, quoted = [], "", False
    for c in name:
        if c == '"':
            quoted = not quoted
        elif c == "." and not quoted:
            parts.append(word.strip())
            word = ""
        else:
            word += c
    parts.append(word.strip())
    return tuple(parts)


def trailing(line: str) -> str:
    """A comment after a value: from a `#` outside a string."""
    quoted = False
    for i, c in enumerate(line):
        if c == '"' and (i == 0 or line[i - 1] != "\\"):
            quoted = not quoted
        elif c == "#" and not quoted:
            return line[i + 1 :].strip()
    return ""


def scan(text: str):
    """The opening block; comments before each member and after it, by path;
    the tables written as headers."""
    lead, block, before, after, headers = [], [], {}, {}, set()
    table, started = (), False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("#"):
            block.append(stripped[1:].strip())
            continue
        if not stripped:
            if not started and block:
                lead, block = lead + block, []
            continue
        if not started and block:
            lead, block = lead + block, []
        started = True
        header = HEADER.match(line)
        if header:
            table = path_of(header.group(1))
            for depth in range(1, len(table) + 1):
                headers.add(table[:depth])
            if block:
                before[table] = block
            block = []
            continue
        key = KEY.match(line)
        if key:
            at = table + (key.group(1).strip('"'),)
            if block:
                before[at] = block
            if note := trailing(line[key.end() :]):
                after[at] = note
        block = []
    return lead, before, after, headers


# Themes and styles: every comment kept where it was, a table written as a
# header an object over lines, one written inline on its line.
def emit(node: dict, path: tuple, depth: int, scanned, out: list[str]) -> None:
    _, before, after, headers = scanned
    pad = "  " * depth
    for index, (k, v) in enumerate(node.items()):
        at = path + (k,)
        expanded = isinstance(v, dict) and at in headers
        if expanded and index > 0:
            out.append("")
        for line in before.get(at, []):
            out.append(f"{pad}//{' ' + line if line else ''}".rstrip())
        if expanded:
            out.append(f"{pad}{key(k)}: {{")
            emit(v, at, depth + 1, scanned, out)
            out.append(f"{pad}}},")
        else:
            note = f"  // {after[at]}" if at in after else ""
            out.append(f"{pad}{key(k)}: {inline(v)},{note}")


def convert(path: Path) -> tuple[Path, str]:
    text = path.read_text(encoding="utf-8")
    data = tomllib.loads(text)
    name = path.name
    lines = header(text)
    if name.endswith(".contract.omui.toml") or name.endswith(".native.omui.toml"):
        kind = "contract" if name.endswith(".contract.omui.toml") else "native"
        stem = name[: -len(f".{kind}.omui.toml")]
        body = data.get(kind, {})
        if list(body) != [stem]:
            raise SystemExit(f"{path}: a TypeScript {kind} file holds one, named as the file ({stem}); this one has {list(body)}")
        function = "defineContract" if kind == "contract" else "defineNative"
        value = body
    elif name.endswith(".ops.omui.toml"):
        function = "defineOps"
        value = dict(data.get("catalog", {}))
        if "operation" in data:
            value["operations"] = data["operation"]
        if "tier" in data:
            value["tiers"] = data["tier"]
        extra = set(data) - {"catalog", "operation", "tier"}
        if extra:
            raise SystemExit(f"{path}: tables omui.gen.data does not carry: {sorted(extra)}")
    elif name.endswith(".theme.omui.toml") or name.endswith(".style.omui.toml"):
        kind = "theme" if name.endswith(".theme.omui.toml") else "style"
        if set(data) != {kind}:
            raise SystemExit(f"{path}: holds {sorted(data)}, not one [{kind}.*]")
        scanned = scan(text)
        lines = [f"//{' ' + line if line else ''}".rstrip() for line in scanned[0]]
        function = "defineTheme" if kind == "theme" else "defineStyle"
        lines += ([""] if lines else []) + [f'import {{ {function} }} from "omui/define";', "", f"export default {function}({{"]
        emit(data[kind], (kind,), 1, scanned, lines)
        lines.append("});")
        return path.with_name(name[: -len(".toml")] + ".ts"), "\n".join(lines) + "\n"
    else:
        raise SystemExit(f"{path}: not a contract, native module, operation catalog, theme or style")
    module = "omui/define" if function == "defineOps" else "openmui/define"  # D-157: the spec's formats, the implementation's
    lines += [f'import {{ {function} }} from "{module}";', "", f"export default {function}({block(value, 0)});"]
    target = path.with_name(name[: -len(".toml")] + ".ts")
    return target, "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    check = "--check" in argv
    files = [Path(a) for a in argv if a != "--check"]
    if not files:
        print(__doc__)
        return 2
    for path in files:
        target, text = convert(path)
        if check:
            print(f"{path} → {target.name} ({len(text.splitlines())} lines)")
        else:
            target.write_text(text, encoding="utf-8")
            print(f"{path} → {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
