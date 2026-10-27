#!/usr/bin/env bash
# A GPU for omui.rhi's tests on every CI row, and OMUI_REQUIRE_GPU=1 so that
# a missing device fails instead of being skipped:
#
#   Linux, Windows  lavapipe (Mesa's Vulkan on the CPU) and the Vulkan loader,
#                   both from xlings; the loader is pointed at lavapipe
#   macOS           the runner's Metal device
#
# Writes the environment to $GITHUB_ENV (stdout without it).
set -euo pipefail
out=${GITHUB_ENV:-/dev/stdout}
say() { echo "$1" >> "$out"; echo "gpu: $1"; }

case "$(uname -s)" in
    Darwin)
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
say "VK_DRIVER_FILES=$icd"
say "VK_ICD_FILENAMES=$icd"
say "OMUI_VULKAN_LIBRARY=$loader"
say "OMUI_REQUIRE_GPU=1"
