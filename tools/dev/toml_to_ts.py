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
  defineSound({ <name>: { layers: […] } }), defineMusic({ name, tempo, …, patches, instruments, … })

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
KEY = re.compile(r"^\s*((?:[A-Za-z0-9_\-]+|\"[^\"]+\")(?:\s*\.\s*(?:[A-Za-z0-9_\-]+|\"[^\"]+\"))*)\s*=")
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


HEADERS = re.compile(r"^\s*\[\[\s*([^\]]+?)\s*\]\]\s*(#.*)?$")


def brackets(text: str) -> int:
    """How many more brackets a line opens than it closes, outside strings and comments."""
    count, quoted = 0, False
    for i, c in enumerate(text):
        if c == '"' and (i == 0 or text[i - 1] != "\\"):
            quoted = not quoted
        elif not quoted and c == "#":
            break
        elif not quoted and c in "[{":
            count += 1
        elif not quoted and c in "]}":
            count -= 1
    return count


def scan(text: str):
    """The opening block; comments before each member and after it, by path
    (an array of tables' item by its index: ("track", 0, "clip", 2)); the
    tables written as headers, and the arrays of tables."""
    lead, block, before, after, headers, arrays = [], [], {}, {}, set(), set()
    table, started, counts, in_string = (), False, {}, False

    def concrete(names: tuple) -> tuple:
        """A header's names with the index of each array of tables it is inside."""
        out = ()
        for name in names:
            out += (name,)
            if out in arrays:
                out += (counts[out] - 1,)
        return out

    arrays_over_lines, open_list = set(), 0
    for line in text.splitlines():
        stripped = line.strip()
        if in_string:  # inside a multi-line string: neither comments nor keys
            in_string = stripped.count('"""') % 2 == 0
            continue
        if open_list > 0:  # inside a list written over lines
            if not stripped.startswith("#"):
                open_list += brackets(line)
            continue
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
        if many := HEADERS.match(line):
            names = path_of(many.group(1))
            parent = concrete(names[:-1])
            path = parent + (names[-1],)
            arrays.add(path)
            for depth in range(1, len(path)):
                headers.add(path[:depth])
            counts[path] = counts.get(path, 0) + 1
            table = path + (counts[path] - 1,)
            if block:
                before[table] = block
            block = []
            continue
        if header := HEADER.match(line):
            table = concrete(path_of(header.group(1)))
            for depth in range(1, len(table) + 1):
                headers.add(table[:depth])
            if block:
                before[table] = block
            block = []
            continue
        if found := KEY.match(line):
            at = table + path_of(found.group(1))
            if block:
                before[at] = block
            rest = line[found.end() :]
            if rest.count('"""') % 2 == 1:
                in_string = True
            elif brackets(rest) > 0:  # a list written over lines: kept over lines
                arrays_over_lines.add(at)
                open_list = brackets(rest)
            elif note := trailing(rest):
                after[at] = note
        block = []
    return lead, before, after, headers, arrays | {("over lines",) + path for path in arrays_over_lines}


class Node:
    """A value with the comments around it, and whether it is written over lines."""

    def __init__(self, value, before=None, after=None, expanded=False):
        self.value, self.before, self.after, self.expanded = value, before or [], after, expanded


def tree(value, path: tuple, scanned) -> Node:
    _, before, after, headers, arrays = scanned
    if isinstance(value, dict):
        members = {k: tree(v, path + (k,), scanned) for k, v in value.items()}
        item_of_tables = bool(path) and isinstance(path[-1], int) and path[:-1] in arrays
        over_lines = path in headers or item_of_tables or any(m.expanded for m in members.values())
        return Node(members, before.get(path), after.get(path), over_lines)
    if isinstance(value, list) and path in arrays:
        items = [tree(v, path + (i,), scanned) for i, v in enumerate(value)]
        for item in items:  # an item on its line when it is short and holds no comment or table of its own
            if isinstance(item.value, dict) and not commented(item) and not any(child.expanded for child in item.value.values()):
                item.expanded = len(literal(plain(item))) > 100
        return Node(items, before.get(path), after.get(path), True)
    if isinstance(value, list):
        over_lines = ("over lines",) + path in arrays  # written over lines: an item a line
        return Node([tree(v, path + (i,), scanned) if isinstance(v, dict) else v for i, v in enumerate(value)], before.get(path), after.get(path), over_lines)
    return Node(value, before.get(path), after.get(path))


def commented(node: Node) -> bool:
    if isinstance(node.value, dict):
        return any(child.before or child.after or commented(child) for child in node.value.values())
    return False


def literal(value) -> str:
    """A value on its line; text over lines as a template literal."""
    if isinstance(value, str) and "\n" in value:
        return "`" + value.replace("\\", "\\\\").replace("`", "\\`").replace("${", "\\${") + "`"
    if isinstance(value, dict):
        return "{ " + ", ".join(f"{key(k)}: {literal(v)}" for k, v in value.items()) + " }" if value else "{}"
    if isinstance(value, list):
        return "[" + ", ".join(literal(v) for v in value) + "]"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return repr(value)
    return json.dumps(value, ensure_ascii=False)


def plain(node: Node):
    """The value without its comments."""
    if isinstance(node.value, dict):
        return {k: plain(v) for k, v in node.value.items()}
    if isinstance(node.value, list):
        return [plain(v) if isinstance(v, Node) else v for v in node.value]
    return node.value


def comments(lines: list[str], pad: str, out: list[str]) -> None:
    for line in lines:
        out.append(f"{pad}//{' ' + line if line else ''}".rstrip())


def emit_members(members: dict, depth: int, out: list[str]) -> None:
    pad = "  " * depth
    for index, (k, node) in enumerate(members.items()):
        if node.expanded and index > 0:
            out.append("")
        comments(node.before, pad, out)
        emit_value(node, f"{pad}{key(k)}: ", depth, out)


def emit_value(node: Node, lead: str, level: int, out: list[str]) -> None:
    depth = level
    pad = "  " * depth
    if node.expanded and isinstance(node.value, dict) and node.value:
        out.append(f"{lead}{{")
        emit_members(node.value, depth + 1, out)
        out.append(f"{pad}}},")
    elif node.expanded and isinstance(node.value, list) and node.value:
        out.append(f"{lead}[")
        inner = "  " * (depth + 1)
        for item in node.value:
            if not isinstance(item, Node):
                item = Node(item)
            comments(item.before, inner, out)
            emit_value(item, inner, depth + 1, out)
        out.append(f"{pad}],")
    else:
        note = f"  // {node.after}" if node.after else ""
        out.append(f"{lead}{literal(plain(node))},{note}")


def rename(members: dict, names: dict) -> dict:
    return {names.get(k, k): v for k, v in members.items()}


def looks(path: Path, text: str, data: dict, kind: str) -> str:
    """A theme, style, sound or music file as its module: every comment where it was."""
    scanned = scan(text)
    root = tree(data, (), scanned).value
    lead = list(scanned[0])
    if kind == "effect":  # one effect a file: its fields, its parameters plural
        members = rename(root, {"param": "params"})
    elif kind in ("theme", "style", "sound", "behavior"):
        extra = set(root) - {kind, "doc"}
        if extra or kind not in root:
            raise SystemExit(f"{path}: holds {sorted(root)}, not one [{kind}.*]")
        if "doc" in root:  # a file's words: its opening comment
            lead += ([""] if lead else []) + str(root["doc"].value).splitlines()
        members = root[kind].value
        if kind == "sound":  # a sound's layers, plural in TypeScript
            for sound in members.values():
                sound.value = rename(sound.value, {"layer": "layers"})
    elif kind == "subtitles":  # the lines, plural
        members = rename(root, {"line": "lines"})
    else:  # music, timeline: the [music] / [timeline] table's fields at the top, the kinds of table plural
        members = {}
        if "doc" in root:
            members["doc"] = root["doc"]
        head = root.get(kind)
        for index, (k, node) in enumerate((head.value if head else {}).items()):
            if index == 0 and head.before:
                node.before = head.before + node.before
            members[k] = node
        plural = {"patch": "patches", "instrument": "instruments", "bus": "buses", "pattern": "patterns", "section": "sections",
                  "marker": "markers", "track": "tracks", "clip": "clips", "property": "properties"}
        for k, node in root.items():
            if k not in ("doc", kind):
                members[plural.get(k, k)] = node
        if kind == "timeline":  # a track's clips, a clip's properties
            def nested(node: Node) -> None:
                if isinstance(node.value, dict):
                    node.value = rename(node.value, {"clip": "clips", "property": "properties"})
                    for child in node.value.values():
                        nested(child)
                elif isinstance(node.value, list):
                    for child in node.value:
                        if isinstance(child, Node):
                            nested(child)
            for node in members.values():
                nested(node)
    function = f"define{kind[0].upper()}{kind[1:]}"
    lines = [f"//{' ' + line if line else ''}".rstrip() for line in lead]
    lines += ([""] if lines else []) + [f'import {{ {function} }} from "omui/define";', "", f"export default {function}({{"]
    emit_members(members, 1, lines)
    lines.append("});")
    return "\n".join(lines) + "\n"


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
    elif (kind := name.split(".")[-3]) in ("theme", "style", "sound", "music", "timeline", "subtitles", "behavior", "effect") and name.endswith(".omui.toml"):
        return path.with_name(name[: -len(".toml")] + ".ts"), looks(path, text, data, kind)
    else:
        raise SystemExit(f"{path}: not a contract, native module, operation catalog, theme, style, sound, music, timeline, subtitles, behavior or effect")
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
