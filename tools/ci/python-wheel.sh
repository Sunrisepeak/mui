#!/usr/bin/env bash
# The Python package as a user installs it (M3 acceptance ⑤): the wheel its
# backend builds (lang/python/packaging), pip-installed into a fresh
# environment. Against the installed package, not the checkout:
#   - the binding's suite passes, example 03's pixels pinned among it;
#   - example 03 runs headless and writes its frame;
#   - the example type-checks against the wheel's stubs (mypy --strict).
set -euo pipefail
cd "$(dirname "$0")/../.."
root=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

python3 -m venv "$work/venv"
if [ -x "$work/venv/bin/python" ]; then py="$work/venv/bin/python"; else py="$work/venv/Scripts/python.exe"; fi
"$py" -m pip install -q --upgrade pip
"$py" -m pip wheel ./lang/python --no-deps -w "$work/dist" --config-settings "profile=${PROFILE:-dev}"
wheel=$(ls "$work"/dist/openmui-*-cp310-abi3-*.whl)
echo "wheel: $(basename "$wheel") ($(du -h "$wheel" | cut -f1))"
"$py" -m pip install -q "$wheel" mypy

cd "$work"  # out of the checkout: `import openmui` is the installed package
"$py" -c "import openmui, os; p = os.path.dirname(openmui.__file__); assert 'site-packages' in p, p; print('openmui from', p)"
OMUI_HEADLESS=1 "$py" -m unittest discover -s "$root/lang/python/tests" -v
"$py" "$root/examples/03-counter/python/tally.py" --headless --png "$work/tally.png"
test -s "$work/tally.png"
"$py" -m mypy --strict --python-version 3.10 "$root/examples/03-counter/python/tally.py"

# The app-py template, as `mcpp new … --template openmui:app-py` makes it,
# against the installed package: its test passes, its script runs.
python3 - "$root/templates/app-py" "$work/pydemo" <<'PY'
import pathlib, re, sys
src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
for f in src.rglob("*"):
    if f.is_dir() or f.name == "template.toml": continue
    rel = f.relative_to(src)
    text = f.read_text(encoding="utf-8")
    if f.suffix == ".in":
        rel = rel.with_suffix("")
        text = re.sub(r"\{\{([a-z_.]+)\}\}", lambda m: {"project.name": "pydemo", "self.version": "0.1.0"}[m.group(1)], text)
    out = dst / rel
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8")
PY
(cd "$work/pydemo" && OMUI_HEADLESS=1 "$py" -m unittest discover -s tests -v)
(cd "$work/pydemo" && "$py" main.py --headless --png "$work/pydemo.png" && test -s "$work/pydemo.png")
"$py" -m mypy --strict --python-version 3.10 "$work/pydemo/main.py"
echo "the wheel installs, its suite passes, example 03 and the app-py template run and type-check"
