// omui.devtools.gpu — a GPU for the device tests (moved from tools/ci/gpu.sh and tools/dev/gpu-real.sh, D-206),
// with OMUI_REQUIRE_GPU=1 so that a missing device fails instead of being skipped.
//
// software  the device CI has. Linux and Windows: lavapipe (Mesa's Vulkan on the CPU) and the Vulkan loader, both
//           from xlings, the loader pointed at lavapipe. macOS: the machine's Metal device, through SDL's cocoa video
//           driver (the offscreen driver makes no Metal views, so SDL_GPU cannot choose Metal under it). On Windows
//           (75655b7, 0dae888: "SDL_HINT_GPU_DRIVER vulkan unsupported!" for every device) a runner's processes are
//           elevated and the Vulkan loader ignores the driver variables there: the manifest, written with an
//           absolute path, is registered under HKLM\SOFTWARE\Khronos\Vulkan\Drivers, and the device is made under
//           SDL's windows video driver, whose surface (VK_KHR_win32_surface) is the one Mesa's Windows build is used
//           with. Direct3D 12's software device (WARP), which SDL would pick before Vulkan, is its own open item
//           (STATUS red list 11).
// real      this machine's NVIDIA GPU (Linux), the hardware a developer has: performance numbers come from it, never
//           from the software device (AGENTS §2.10). The driver comes the way xlings gives it —
//           `nvidia-gl-host-link`: stable links to the host's own NVIDIA userspace and a Vulkan driver manifest naming
//           them by absolute path — with the packaged Vulkan loader; the driver opens its private halves by name,
//           so the package's library directory is put on the search path (nothing else of the system's is). No
//           window is needed: SDL's offscreen video driver.
export module omui.devtools.gpu;

import std;

export namespace omui::devtools::gpu {

enum class Device { software, real };

struct Environment {
    std::vector<std::pair<std::string, std::string>> variables;  // in the order they are set
    std::vector<std::string> said;                               // what was found and checked, a line each
};

// The device installed where it is missing, found and checked; the variables that select it.
[[nodiscard]] std::expected<Environment, std::string> prepare(Device device);

// An ELF file's machine ("x86_64", "aarch64", or its number), or nothing where it is not ELF.
[[nodiscard]] std::optional<std::string> elf_machine(const std::filesystem::path& file);

// a < b as `sort -V` orders them: runs of digits by their value, the rest by character.
[[nodiscard]] bool version_less(std::string_view a, std::string_view b);

}  // namespace omui::devtools::gpu
