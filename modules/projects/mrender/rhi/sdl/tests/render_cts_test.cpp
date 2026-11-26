// The render contract's suite (mrender.cts) on SDL_GPU, on whatever device
// this machine has (lavapipe in Linux CI): GPU pixels through mrender's
// renderer against the CPU reference, the render graph, the mask atlas,
// uploads, external textures, effect output bounds. With OMUI_REQUIRE_GPU=1
// (CI) a missing device fails; otherwise it is said and the case passes
// vacuously. The report is kept beside the test's scratch files.
import std;
import mlibs.rhi;
import mlibs.cts;
import mrender.rhi.sdl;
import mrender.cts;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;

UTest cases{"mrender.cts.sdl", [] {
    "the render suite passes on this device, every case"_test = [] {
        mrender::rhi::sdl::Options o;
        o.formats = mrender::cts::shader_formats();
        o = environment_options(std::move(o));  // OMUI_VULKAN_LIBRARY, OMUI_GPU_VIDEO_DRIVER
        auto device = mrender::rhi::sdl::create(o);
        if (!device) {
            if (mrender::rhi::sdl::device_required()) expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", device.error().message);
            else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", device.error().message);
            return;
        }
        const auto dir = scratch_dir("mrender-cts");
        // The suite's cases, this program's own: <device>/render.graph.layers-and-culling, …
        mrender::cts::declare(**device, {.subject = (*device)->caps().device}, {.scratch = dir});
    };
}};
