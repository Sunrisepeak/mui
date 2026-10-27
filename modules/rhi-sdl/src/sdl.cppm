// openmui.rhi.sdl — omui.rhi on SDL_GPU (architecture §8.2, D-33): Vulkan,
// Metal or Direct3D 12, whichever SDL finds for the shader formats the
// program's shaders carry. A thin mapping: rhi's descriptions to SDL's,
// a submission's commands to one SDL command buffer (uploads staged once,
// copy passes around the render and compute passes in the list's order), a
// fence per submission, readback through a download buffer.
//
// A Device is used from one thread at a time. SDL's video subsystem is
// initialised while a device exists (a window can be claimed later); on a
// machine with no display, SDL_VIDEO_DRIVER=offscreen.
export module openmui.rhi.sdl;

import std;
import openmui.rhi;

export namespace omui::rhi::sdl {

struct Options {
    // The formats the program's shaders are compiled to: SDL picks a backend
    // that takes one of them (openmui.slang's formats_for(target)).
    ShaderFormats formats = omui::rhi::formats({ShaderFormat::spirv});
    // A backend to insist on; none: SDL's choice for the platform.
    std::optional<Backend> backend;
    bool debug = false;             // the backend's validation layer, when installed
    bool prefer_low_power = false;  // an integrated GPU over a discrete one
    // The Vulkan loader to use (SDL_HINT_VULKAN_LIBRARY); empty: the system's.
    std::string vulkan_library;
};

// A device, or why there is none (Errc::unsupported: no GPU, driver or
// backend for those formats; the caller falls back to the CPU renderer).
[[nodiscard]] Result<std::unique_ptr<Device>> create(const Options& options = {});

// The backends this build of SDL can drive, in its order of preference.
[[nodiscard]] std::vector<Backend> backends();

}  // namespace omui::rhi::sdl
