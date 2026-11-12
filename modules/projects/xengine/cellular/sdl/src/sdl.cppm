// xengine.cellular.sdl — a native device for the cellular kernel, as this
// machine and its environment give one: SDL_GPU taking the shader formats
// asked for, the Vulkan library and SDL's video driver named by
// OMUI_VULKAN_LIBRARY and OMUI_GPU_VIDEO_DRIVER when they are set (a machine
// without a display; CI's rows).
export module xengine.cellular.sdl;

import std;
import mlibs.rhi;
import xrender.rhi.sdl;
import mcpplibs.os;

export namespace xengine::cellular::sdl {

// The device, or why there is none.
[[nodiscard]] std::expected<std::unique_ptr<mlibs::rhi::Device>, std::string> open(mlibs::rhi::ShaderFormats formats) {
    xrender::rhi::sdl::Options options;
    options.formats = formats;
    if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *library;
    if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *video;
    auto device = xrender::rhi::sdl::create(options);
    if (!device) return std::unexpected(device.error().message);
    return std::move(*device);
}

// Whether a missing device is a failure (CI: OMUI_REQUIRE_GPU=1) or only said.
[[nodiscard]] bool required() { return mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1"; }

}  // namespace xengine::cellular::sdl
