// hanim.rhi's conformance suite on SDL_GPU, on whatever device this machine
// has (lavapipe in Linux CI): the owner of a suite runs it against a backend
// (D-161), never a backend against its consumers. It is hanim.studio's test
// because hanim.rhi runs on the Web too and this backend does not. With
// OMUI_REQUIRE_GPU=1 (CI) a missing device fails; otherwise it is said and the
// case passes vacuously.
import std;
import mlibs.rhi;
import mlibs.cts;
import xrender.rhi.sdl;
import hanim.rhi;
import hanim.rhi.cts;
import mcpplibs.os;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;

UTest cases{"hanim.rhi.sdl", [] {
    "shape batches: every control point equal to the CPU sampler's within float error"_test = [] {
        xrender::rhi::sdl::Options options;
        options.formats = hanim::gpu::shader_formats();
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *video;
        auto device = xrender::rhi::sdl::create(options);
        if (!device) {
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", device.error().message);
            else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", device.error().message);
            return;
        }
        const auto& caps = (*device)->caps();
        std::println("device: {} on {}{}", caps.device, name(caps.backend), caps.software ? " (software)" : "");
        hanim::gpu::cts::Totals totals;
        // The suite's cases, this program's own: <device>/shape.batch.straight, …
        hanim::gpu::cts::declare(**device, {.subject = caps.device, .required = {"compute"}}, &totals);
        expect(eq(totals.scenarios, 4u));
        std::println("shape batches: {} samples, {} points, largest difference {}", totals.samples, totals.compared_points, totals.largest_difference);
    };
}};
