#!/usr/bin/env bash
# The templates (component-binding §10.1) are what `mcpp new --template openmui[:name]`
# instantiates. Until openmui is in mcpp-index, this renders them the way mcpp does
# ({{var}} in *.in files, the suffix dropped), points their openmui dependencies at
# this checkout, and builds and tests each one.
set -euo pipefail
cd "$(dirname "$0")/../.."
root=$(pwd)
work=$(mktemp -d)
# Out of this tree a project reads the global index: make sure it is there.
(cd "$work" && mcpp index update > /dev/null 2>&1) || true

render() {  # render <template> <destination> <project name>
    python3 - "$root/templates/$1" "$2" "$3" "$root" <<'PY'
import sys, pathlib, re
src, dst, name, root = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3], sys.argv[4]
vars = {"project.name": name, "self.name": "openmui.sdk", "self.version": "0.1.0",
        "template.package.namespace": "openmui", "template.package.name": "sdk"}
for f in src.rglob("*"):
    if f.is_dir() or f.name == "template.toml": continue
    rel = f.relative_to(src)
    text = f.read_bytes()
    if f.suffix == ".in":
        rel = rel.with_suffix("")
        text = re.sub(r"\{\{([a-z_.]+)\}\}", lambda m: vars[m.group(1)], text.decode()).encode()
    out = dst / rel
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(text)
# This checkout instead of the published packages.
paths = {"openmui.sdk": ".", "openmui.entry.player": "src/entry/player", "openmui.player": "runtime/player",
         "openmui.lang.js": "lang/js", "openmui.lang.lua": "lang/lua", "openmui.native": "runtime/native",
         "openmui.theme.default": "themes/default"}
m = dst / "mcpp.toml"
if m.exists():
    t = m.read_text()
    for pkg, rel in paths.items():
        t = re.sub(r'^"?' + re.escape(pkg) + r'"? = "0\.1\.0"$', f'{pkg} = {{ path = "{root}/{rel}" }}', t, flags=re.M)
    t = t.replace('openmui.rules = { version = "0.1.0", host-module = true }', f'openmui.rules = {{ path = "{root}/mcpp/rules", host-module = true }}')
    m.write_text(t)
PY
}

# mcpp 2026.10.26.1 offers a path build-dependency to a build program only
# from a workspace that already builds it (its own examples/08-build-rules
# fails out of tree), so the component template is checked as a temporary
# member of this workspace; with the rule from the index this is not needed.
check="$root/.template-check"
cp "$root/mcpp.toml" "$work/mcpp.toml.saved"
cleanup() { cp "$work/mcpp.toml.saved" "$root/mcpp.toml"; rm -rf "$check" "$work"; }
trap cleanup EXIT

# A template whose package runs a build rule (component, native, theme) is
# checked in the workspace; an application out of it, as a user makes one.
for t in app-cpp:demo app-ts:tsdemo app-lua:luademo component:widgets native:wordkit theme:brandkit; do
    template=${t%%:*}; name=${t#*:}
    echo "== $template ($name)"
    case "$template" in
        component|native|theme)
            mkdir -p "$check"
            render "$template" "$check/$name" "$name"
            python3 - "$root/mcpp.toml" "$name" <<'PY'
import sys
p, name = sys.argv[1], sys.argv[2]; s = open(p).read()
s = s.replace('    "tests/rules",\n', f'    "tests/rules",\n    ".template-check/{name}",\n')
open(p, "w").write(s)
PY
            sed -i "s|path = \"$root/|path = \"../../|g; s|path = \"$root\" }|path = \"../..\" }|g" "$check/$name/mcpp.toml"
            (cd "$root" && mcpp test -p "$name" 2>&1 | grep -E "test result|FAIL|error" ) || { echo "FAIL $template"; exit 1; }
            (cd "$root" && mcpp test -p "$name" 2>&1 | grep -q "test result ok") || { echo "FAIL $template"; exit 1; }
            ;;
        *)
            render "$template" "$work/$name" "$name"
            (cd "$work/$name" && mcpp build > build.log 2>&1 || { cat build.log; exit 1; })
            (cd "$work/$name" && mcpp test 2>&1 | grep -E "test result|FAIL" ) || { echo "FAIL $template"; exit 1; }
            (cd "$work/$name" && mcpp test 2>&1 | grep -q "test result ok") || { echo "FAIL $template"; exit 1; }
            ;;
    esac
done
echo "templates: ok"
