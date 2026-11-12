// hanim.rhi's conformance suite on SDL_GPU, on whatever device this machine
// has (lavapipe in Linux CI): the owner of a suite runs it against a backend
// (D-161), never a backend against its consumers. With OMUI_REQUIRE_GPU=1
// (CI) a missing device fails; otherwise it is said and the case passes
// vacuously.
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
        const auto report = hanim::gpu::cts::run(**device, {.subject = caps.device, .platform = "sdl"}, {.required = {"compute"}}, &totals);
        std::print("{}", report.text());
        expect(report.ok() && report.count(mlibs::cts::Status::passed) == report.outcomes.size()) << report.text();
        expect(eq(totals.scenarios, 4u));
        std::println("shape batches: {} samples, {} points, largest difference {}", totals.samples, totals.compared_points, totals.largest_difference);
        std::ofstream(scratch_dir("rhi") / "sdl-shape.cts.omui.json") << report.json();  // the evidence, for a reviewer
    };
}};
