#!/usr/bin/env bash
# A GPU for omui.rhi's tests on every CI row, and OMUI_REQUIRE_GPU=1 so that
# a missing device fails instead of being skipped:
#
#   Linux, Windows  lavapipe (Mesa's Vulkan on the CPU) and the Vulkan loader,
#                   both from xlings; the loader is pointed at lavapipe
#   macOS           the runner's Metal device, through SDL's cocoa video
#                   driver: the offscreen driver makes no Metal views, so
#                   SDL_GPU cannot choose Metal under it
#
# Writes the environment to $GITHUB_ENV (stdout without it).
set -euo pipefail
out=${GITHUB_ENV:-/dev/stdout}
say() { echo "$1" >> "$out"; echo "gpu: $1"; }

case "$(uname -s)" in
    Darwin)
        say "OMUI_GPU_VIDEO_DRIVER=cocoa"
        say "OMUI_REQUIRE_GPU=1"
        exit 0
        ;;
esac

for attempt in 1 2 3; do
    if xlings install mesa-lavapipe vulkan-loader -y -g; then break; fi
    [ "$attempt" = 3 ] && { echo "::error::lavapipe could not be installed"; exit 1; }
    sleep 30
done
root="$HOME/.xlings/data/xpkgs"
icd=$(find "$root" -path '*mesa-lavapipe*' -name 'lvp_icd*.json' | sort | tail -1)
[ -n "$icd" ] || { echo "::error::lavapipe's driver manifest (lvp_icd*.json) is not where xlings installs"; exit 1; }
case "$(uname -s)" in
    Linux)
        loader=$(find "$root" -path '*vulkan-loader*' -name 'libvulkan.so.1' | sort | tail -1)
        ;;
    *)
        loader=$(find "$root" -path '*vulkan-loader*' -iname 'vulkan-1.dll' | sort | tail -1)
        icd=$(cygpath -w "$icd")
        loader=$(cygpath -w "$loader")
        ;;
esac
[ -n "$loader" ] || { echo "::error::the Vulkan loader is not where xlings installs"; exit 1; }
if [ "$(uname -s)" = Linux ]; then
    # xim's vulkan-loader has one Linux payload, x86_64, and installs it on
    # every machine: on aarch64 it is a library of the wrong architecture. The
    # ELF header says so; there, the distribution's loader is used instead
    # (lavapipe itself is aarch64 from xim).
    machine() { python3 -c 'import sys; d=open(sys.argv[1],"rb").read(20); print({62:"x86_64",183:"aarch64"}.get(int.from_bytes(d[18:20],"little"),"?"))' "$1"; }
    if [ "$(machine "$loader")" != "$(uname -m)" ]; then
        echo "gpu: xim's Vulkan loader is $(machine "$loader") on an $(uname -m) machine; using the distribution's"
        system=/usr/lib/$(uname -m)-linux-gnu/libvulkan.so.1
        [ -e "$system" ] || sudo apt-get install -y --no-install-recommends libvulkan1 > /dev/null
        [ -e "$system" ] || { echo "::error::no Vulkan loader for $(uname -m)"; exit 1; }
        loader=$system
    fi
    # What the loader and the driver resolve to, so a device that does not
    # come up says why; the loader reports its own errors on stderr.
    driver=$(dirname "$icd")/../../../lib/libvulkan_lvp.so
    for so in "$loader" "$driver"; do
        echo "gpu: $so"
        ldd "$so" | sed 's/^/gpu:   /'
        if ldd "$so" | grep -q 'not found'; then echo "::error::$so has unresolved libraries"; exit 1; fi
    done
    say "VK_LOADER_DEBUG=error,warn"
fi
say "VK_DRIVER_FILES=$icd"
say "VK_ICD_FILENAMES=$icd"
say "OMUI_VULKAN_LIBRARY=$loader"
say "OMUI_REQUIRE_GPU=1"
