#!/usr/bin/env bash
# The showcases' README media. Each showcase's tests draw its signature shot
# headless (a still, or an animated WebP or PNG under 1 MB) into the scratch directory
# readme-<showcase>; the files are deterministic, so they are committed in
# showcase/<showcase>/media/ and CI holds them to what the tests draw.
#
# omui's identity study is covered too: its tests draw the mark's size sheet
# and live study into scratch_dir("brand"), committed under site/media/.
#
#     tools/ci/readme-media.sh [showcase… | site]          # run the tests, copy what they drew
#     tools/ci/readme-media.sh --check [showcase… | site]  # after the tests: the committed media are what they drew
set -euo pipefail
cd "$(dirname "$0")/../.."
MCPP=${MCPP:-mcpp}
check=0
if [ "${1:-}" = "--check" ]; then check=1; shift; fi
scratch=$(python3 -c 'import tempfile, os; print(os.path.join(tempfile.gettempdir(), "openmui-tests"))')
shows=("$@")
if [ ${#shows[@]} -eq 0 ]; then
    for d in showcase/*/; do d=${d%/}; [ -f "$d/mcpp.toml" ] && shows+=("${d#showcase/}"); done
    shows+=(site)
fi
status=0
for show in "${shows[@]}"; do
    src="$scratch/readme-$show"
    folder="showcase/$show"
    if [ "$show" = site ]; then folder=site; src="$scratch/brand"; fi
    if [ $check = 0 ]; then
        pkg=$(python3 -c 'import sys, tomllib; p = tomllib.load(open(sys.argv[1], "rb"))["package"]; print(p["namespace"] + "." + p["name"])' "$folder/mcpp.toml")
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
            if ! cmp -s "$f" "$folder/media/$name"; then
                echo "::error::$folder/media/$name is not what the tests draw: run tools/ci/readme-media.sh $show" >&2
                status=1
            fi
        done
    else
        mkdir -p "$folder/media"
        cp "$src"/* "$folder/media/"
        ls -l "$folder/media/"
    fi
    if [ "$show" = site ]; then
        for name in home-en-desktop docs-en-desktop explore-en-desktop hello-en-desktop docs-zh-phone features-zh-phone; do
            source="$scratch/site-structure/$name.png"
            destination="$folder/media/structure-$name.png"
            if [ $check = 1 ]; then
                if ! cmp -s "$source" "$destination"; then
                    echo "::error::$destination is not what the site's layout tests draw" >&2
                    status=1
                fi
            else
                cp "$source" "$destination"
            fi
        done
        for name in overview-en-1280 button-en-1280 text_box-en-1280 data_grid-en-1280 overview-zh-390 button-zh-390 text_box-zh-390 code_editor-zh-390; do
            source="$scratch/site-catalog/$name.png"
            destination="$folder/media/catalog-$name.png"
            if [ $check = 1 ]; then
                if ! cmp -s "$source" "$destination"; then
                    echo "::error::$destination is not what the component layout tests draw" >&2
                    status=1
                fi
            else
                cp "$source" "$destination"
            fi
        done
    fi
done
exit $status
