#!/usr/bin/env bash
# The showcases' README media. Each showcase's tests draw its signature shot
# headless (a still, or an animated PNG under 1 MB) into the scratch directory
# readme-<showcase>; the files are deterministic, so they are committed in
# showcase/<showcase>/media/ and CI holds them to what the tests draw.
#
#     tools/ci/readme-media.sh [showcase…]          # run the tests, copy what they drew
#     tools/ci/readme-media.sh --check [showcase…]  # after the tests: the committed media are what they drew
set -euo pipefail
cd "$(dirname "$0")/../.."
MCPP=${MCPP:-mcpp}
check=0
if [ "${1:-}" = "--check" ]; then check=1; shift; fi
scratch=$(python3 -c 'import tempfile, os; print(os.path.join(tempfile.gettempdir(), "openmui-tests"))')
shows=("$@")
if [ ${#shows[@]} -eq 0 ]; then
    for d in showcase/*/; do d=${d%/}; [ -f "$d/mcpp.toml" ] && shows+=("${d#showcase/}"); done
fi
status=0
for show in "${shows[@]}"; do
    src="$scratch/readme-$show"
    if [ $check = 0 ]; then
        pkg=$(python3 -c 'import sys, tomllib; p = tomllib.load(open(sys.argv[1], "rb"))["package"]; print(p["namespace"] + "." + p["name"])' "showcase/$show/mcpp.toml")
        echo "readme-media: $show ($pkg)"
        "$MCPP" test -p "$pkg" >/dev/null
    fi
    if [ ! -d "$src" ] || [ -z "$(ls -A "$src")" ]; then
        echo "::error::readme-media: $show drew nothing (a test writing scratch_dir(\"readme-$show\"))" >&2
        status=1
        continue
    fi
    if [ $check = 1 ]; then
        for f in "$src"/*; do
            name=$(basename "$f")
            if ! cmp -s "$f" "showcase/$show/media/$name"; then
                echo "::error::showcase/$show/media/$name is not what the tests draw: run tools/ci/readme-media.sh $show" >&2
                status=1
            fi
        done
    else
        mkdir -p "showcase/$show/media"
        cp "$src"/* "showcase/$show/media/"
        ls -l "showcase/$show/media/"
    fi
done
exit $status
