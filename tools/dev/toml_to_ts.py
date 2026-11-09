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

The file's leading comment stays, as `//` lines. Tables of scalars print on
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
    else:
        raise SystemExit(f"{path}: not a contract, native module or operation catalog")
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
