#!/usr/bin/env python3
"""Each test file's package given mtest as a dev-dependency (W0, D-147).

    tools/dev/mtest_dev_dependency.py FILE…   the nearest mcpp.toml above each file

A package whose tests moved to mtest (tools/dev/to_mtest.py) names it once:
[dev-dependencies] mcpplibs.mtest = "0.5.0". Manifests that name it already
are left as they are.
"""
import re
import sys
from pathlib import Path

VERSION = '"0.5.0"'


def manifest_of(file: Path) -> Path | None:
    for parent in file.resolve().parents:
        if (parent / "mcpp.toml").is_file():
            return parent / "mcpp.toml"
    return None


def main(argv: list[str]) -> int:
    done = set()
    for name in argv:
        manifest = manifest_of(Path(name))
        if manifest is None or manifest in done:
            continue
        done.add(manifest)
        text = manifest.read_text(encoding="utf-8")
        if re.search(r"^mcpplibs\.mtest\s*=", text, re.M):
            continue
        section = re.search(r"^\[dev-dependencies\]\n", text, re.M)
        if section:
            text = text[:section.end()] + f"mcpplibs.mtest = {VERSION}\n" + text[section.end():]
        else:
            text = text.rstrip("\n") + f"\n\n[dev-dependencies]\nmcpplibs.mtest = {VERSION}\n"
        manifest.write_text(text, encoding="utf-8")
        print(f"{manifest}: mcpplibs.mtest {VERSION}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
