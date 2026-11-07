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

using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite s{"xrender.cts.sdl"};
    s.test("the render suite passes on this device, every case", [] {
        xrender::rhi::sdl::Options o;
        o.formats = xrender::cts::shader_formats();
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
        auto device = xrender::rhi::sdl::create(o);
        if (!device) {
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") check(false, std::format("OMUI_REQUIRE_GPU=1 and no device: {}", device.error().message));
            else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", device.error().message);
            return;
        }
        const auto dir = scratch_dir("xrender-cts");
        const auto report = xrender::cts::run(**device, {.subject = (*device)->caps().device, .platform = "sdl"}, {}, {.scratch = dir});
        std::ofstream(dir / "sdl.render.cts.omui.json") << report.json();  // the evidence, for a reviewer
        check_eq(report.outcomes.size(), xrender::cts::cases().size());
        check(report.ok() && report.count(mlibs::cts::Status::passed) == xrender::cts::cases().size(), report.text());
    });
    return s.run(argc, argv);
}
