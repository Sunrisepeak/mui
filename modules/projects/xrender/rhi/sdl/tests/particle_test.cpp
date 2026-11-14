// The particles' contract (xrender.particle.gpu.cts) on SDL_GPU, on whatever device this machine has (lavapipe in
// Linux CI): the same batches over the same frame as the CPU reference (xrender.particle). With
// OMUI_REQUIRE_GPU=1 (CI) a missing device fails; otherwise it is said and the case passes vacuously.
import std;
import mlibs.rhi;
import mlibs.cts;
import xrender.rhi.sdl;
import xrender.particle.gpu.cts;
import mcpplibs.os;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"particle.gpu.sdl", [] {
    "the particles' suite passes on this device, every case"_test = [] {
        xrender::rhi::sdl::Options options;
        options.formats = xrender::particle::gpu::cts::shader_formats();
        if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *library;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *video;
        auto device = xrender::rhi::sdl::create(options);
        if (!device) {
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", device.error().message);
            else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", device.error().message);
            return;
        }
        expect(eq(xrender::particle::gpu::cts::cases().size(), std::size_t{1}));
        xrender::particle::gpu::cts::declare(**device, {.subject = (*device)->caps().device});
    };
}};
