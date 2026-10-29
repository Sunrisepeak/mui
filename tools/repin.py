#!/usr/bin/env python3
"""Re-pin hashes from a test run: after the pictures were looked at.

    mcpp test … 2>&1 | python3 tools/repin.py [--dry-run]

Reads `openmui.testing`'s failures — `<file>:<line>: check failed: expected
<old>, got <new>` — and, on that very line, puts <new> where <old> stood. A
pin is a 64-bit number (printed in decimal, written `0x` and 16 hex digits)
or a hex string (`"03892ce45ed9729c"`). A line where the old value is not
found exactly once is left alone and named, so nothing is guessed. Answers
what changed; exits 1 when a failure could not be placed.
"""
import re
import sys

FAIL = re.compile(r"^\s*(?P<file>/\S+):(?P<line>\d+): check failed: expected (?P<old>[0-9a-fA-F]+), got (?P<new>[0-9a-fA-F]+)\s*$")


def forms(value: str) -> list[str]:
    """How a printed value may stand in the source: a hex string as itself; a decimal as 0x and 16 digits."""
    out = [value]
    if value.isdigit() and len(value) > 16:  # a uint64 printed in decimal
        out = [f"0x{int(value):016x}", f"0x{int(value):x}"]
    return out


def main() -> int:
    dry = "--dry-run" in sys.argv[1:]
    edits: dict[str, dict[int, tuple[str, str]]] = {}
    for raw in sys.stdin:
        m = FAIL.match(raw.rstrip("\n"))
        if m:
            edits.setdefault(m["file"], {})[int(m["line"])] = (m["old"], m["new"])
    left = 0
    for path, lines in edits.items():
        with open(path, encoding="utf-8") as f:
            text = f.read().split("\n")
        for number, (old, new) in sorted(lines.items()):
            line = text[number - 1]
            placed = False
            for o, n in zip(forms(old), forms(new)):
                if line.count(o) == 1:
                    text[number - 1] = line.replace(o, n)
                    print(f"{path}:{number}: {o} -> {n}")
                    placed = True
                    break
            if not placed:
                print(f"{path}:{number}: `{old}` not found exactly once; left alone", file=sys.stderr)
                left += 1
        if not dry:
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(text))
    return 1 if left else 0


if __name__ == "__main__":
    sys.exit(main())
