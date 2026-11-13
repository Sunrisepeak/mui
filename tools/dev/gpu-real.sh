#!/usr/bin/env bash
# Runs a command on this machine's real GPU (Linux, an NVIDIA driver): tools/ci/gpu.sh is the software device CI
# has; this is the hardware a developer has. The programs mcpp builds load libraries with their own C library
# and do not search the system's directories, so the Vulkan loader is the packaged one, the driver is named by
# an absolute path, and the driver's own libraries (and nothing else of the system's) are put on the search
# path. No window is needed: SDL's offscreen video driver.
#
#   tools/dev/gpu-real.sh mcpp test -p xrender.rhi.sdl mesh_test
set -euo pipefail
driver=$(ls /usr/lib/x86_64-linux-gnu/libGLX_nvidia.so.0 /usr/lib64/libGLX_nvidia.so.0 2>/dev/null | head -1 || true)
[ -n "$driver" ] || { echo "gpu-real.sh: no NVIDIA Vulkan driver (libGLX_nvidia.so.0) on this machine" >&2; exit 1; }
loader=$(find "$HOME/.xlings/data/xpkgs" -path '*vulkan-loader*' -name libvulkan.so.1 2>/dev/null | sort | tail -1)
[ -n "$loader" ] || { echo "gpu-real.sh: no packaged Vulkan loader (run tools/ci/gpu.sh once: it installs one)" >&2; exit 1; }
kept="${TMPDIR:-/tmp}/omui-gpu-real"
mkdir -p "$kept/lib"
for library in "$(dirname "$driver")"/libnvidia*.so* "$(dirname "$driver")"/libGLX_nvidia.so* "$(dirname "$driver")"/libEGL_nvidia.so*; do
  [ -e "$library" ] && ln -sf "$library" "$kept/lib/"
done
printf '{ "file_format_version": "1.0.1", "ICD": { "library_path": "%s", "api_version": "1.3.0" } }\n' "$driver" > "$kept/icd.json"
export VK_DRIVER_FILES="$kept/icd.json" VK_ICD_FILENAMES="$kept/icd.json" OMUI_VULKAN_LIBRARY="$loader"
export LD_LIBRARY_PATH="$kept/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_VIDEO_DRIVER=offscreen OMUI_GPU_VIDEO_DRIVER=offscreen OMUI_REQUIRE_GPU=1
exec "$@"
