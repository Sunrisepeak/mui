#!/usr/bin/env bash
# Runs a command on this machine's real GPU (Linux, an NVIDIA driver): tools/ci/gpu.sh is the software device CI
# has; this is the hardware a developer has. The driver comes the way the xlings ecosystem gives it —
# `nvidia-gl-host-link`: stable links to the host's own NVIDIA userspace and a Vulkan driver manifest naming
# them by absolute path — with the packaged Vulkan loader. The driver opens its private halves by name, so the
# package's library directory is put on the search path (nothing else of the system's is). No window is needed:
# SDL's offscreen video driver.
#
#   tools/dev/gpu-real.sh mcpp test -p xrender.rhi.sdl mesh_test
set -euo pipefail
root="$HOME/.xlings/data/xpkgs"
linked() { find "$root" -path '*nvidia-gl-host-link*/share/vulkan/icd.d/10_nvidia.json' 2>/dev/null | sort -V | tail -1; }
loaded() { find "$root" -path '*vulkan-loader*' -name libvulkan.so.1 2>/dev/null | sort -V | tail -1; }
manifest=$(linked)
loader=$(loaded)
if [ -z "$manifest" ] || [ -z "$loader" ]; then
  xlings install nvidia-gl-host-link vulkan-loader -y -g
  manifest=$(linked)
  loader=$(loaded)
fi
[ -n "$manifest" ] && [ -n "$loader" ] || { echo "gpu-real.sh: no NVIDIA driver linked by xlings on this machine (xlings-gl-doctor says why)" >&2; exit 1; }
package=${manifest%/share/vulkan/icd.d/10_nvidia.json}
export VK_DRIVER_FILES="$manifest" VK_ICD_FILENAMES="$manifest" OMUI_VULKAN_LIBRARY="$loader"
export LD_LIBRARY_PATH="$package/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_VIDEO_DRIVER=offscreen OMUI_GPU_VIDEO_DRIVER=offscreen OMUI_REQUIRE_GPU=1
exec "$@"
