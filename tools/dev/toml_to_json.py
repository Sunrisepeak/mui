#!/usr/bin/env python3
"""A TOML data file as its JSON (W1 step 3, D-139): `<name>.<kind>.omui.toml`
→ `<name>.<kind>.omui.json`, its comments kept as `doc` — the file's leading
block the document's, a block before a table or an entry that table's, one before a key `<key>_doc`
beside it, a marker before a later entry its `note` (readers take the keys
they know; doc, note and *_doc are documentation everywhere).

    tools/dev/toml_to_json.py FILE…      write the JSON beside each, remove the TOML
"""
import json
import re
import sys
import tomllib
from pathlib import Path

HEADER = re.compile(r"^\s*(\[\[?)\s*([^\]]+?)\s*(\]\]?)\s*(#.*)?$")


def path_of(name: str) -> list[str]:
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
    return parts


KEY = re.compile(r"^\s*([A-Za-z0-9_\-]+|\"[^\"]+\")\s*=")


def docs(text: str):
    """The leading block, and each comment block with what it comes before:
    (path, index, None, doc) a table, (path, index, key, doc) a key in it."""
    lead, block, started, counts, found = [], [], False, {}, []
    table, index = (), None
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("#"):
            block.append(stripped[1:].strip())
            continue
        if not stripped:
            if not started and block and not lead:
                lead, block = block, []
            continue
        words = " ".join(w for w in block if w).strip()
        block = []
        header = HEADER.match(line)
        if header:
            table = tuple(path_of(header.group(2)))
            index = None
            if header.group(1) == "[[":
                index = counts.get(table, 0)
                counts[table] = index + 1
            if words:
                found.append((table, index, None, words))
            started = True
            continue
        key = KEY.match(line)
        if key and words and not started and not lead and table == ():
            lead = [words]  # the file's opening words, right above its first key
        elif key and words:
            found.append((table, index, key.group(1).strip('"'), words))
        started = True
    return " ".join(w for w in lead if w).strip(), found


def convert(src: Path) -> Path:
    text = src.read_text(encoding="utf-8")
    data = tomllib.loads(text)
    lead, found = docs(text)
    for name, index, key, doc in found:
        node = data
        for part in name:
            node = node[part]
        if index is not None:
            node = node[index]
        if not isinstance(node, dict):
            continue
        if key is None and index is None and node and all(isinstance(v, dict) for v in node.values()) and len(name) > 1:
            # A map of entries (a contract's commands): its words beside it, on its parent — inside, `doc` would be one of them.
            parent = data
            for part in name[:-1]:
                parent = parent[part]
            parent.setdefault(f"{name[-1]}_doc", doc)
            continue
        if key is None and index == 0:
            # A section's prose before its first entry: the section's, on the document — not the first entry's own.
            holder = data
            for part in name[:-1]:
                holder = holder[part]
            holder.setdefault(f"{name[-1]}_doc", doc)
            continue
        field = "doc" if key is None else f"{key}_doc"
        if field not in node:
            node[field] = doc
        elif key is None:
            node.setdefault("note", doc)  # a marker between entries (a version's), on the entry it precedes
    out = {"doc": lead} if lead and "doc" not in data else {}
    out.update(data)
    dst = src.with_suffix(".json")
    dst.write_text(json.dumps(out, ensure_ascii=False, indent=2, default=str) + "\n", encoding="utf-8")
    src.unlink()
    return dst


def main(argv: list[str]) -> int:
    for name in argv:
        print(convert(Path(name)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
