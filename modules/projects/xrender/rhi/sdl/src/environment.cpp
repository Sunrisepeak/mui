// xrender.rhi.sdl's options from the environment (api.cppm): one place reads the variables `omui-devtools gpu` sets.
module xrender.rhi.sdl.api;

import std;
import mcpplibs.os;

namespace xrender::rhi::sdl {

Options environment_options(Options base) {
    if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) base.vulkan_library = *library;
    if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) base.video_driver = *video;
    return base;
}

bool device_required() { return mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1"; }

}  // namespace xrender::rhi::sdl
