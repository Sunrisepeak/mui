// The render contract's suite (xrender.cts) on SDL_GPU, on whatever device
// this machine has (lavapipe in Linux CI): GPU pixels through xrender's
// renderer against the CPU reference, the render graph, the mask atlas,
// uploads, external textures, effect output bounds. With OMUI_REQUIRE_GPU=1
// (CI) a missing device fails; otherwise it is said and the case passes
// vacuously. The report is kept beside the test's scratch files.
import std;
import mlibs.rhi;
import mlibs.cts;
import xrender.rhi.sdl;
import xrender.cts;
import mcpplibs.os;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;

UTest cases{"xrender.cts.sdl", [] {
    "the render suite passes on this device, every case"_test = [] {
        xrender::rhi::sdl::Options o;
        o.formats = xrender::cts::shader_formats();
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
        auto device = xrender::rhi::sdl::create(o);
        if (!device) {
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", device.error().message);
            else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", device.error().message);
            return;
        }
        const auto dir = scratch_dir("xrender-cts");
        // The suite's cases, this program's own: <device>/render.graph.layers-and-culling, …
        xrender::cts::declare(**device, {.subject = (*device)->caps().device}, {.scratch = dir});
    };
}};
