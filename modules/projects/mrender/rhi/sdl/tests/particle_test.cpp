// The particles' contract (mrender.particle.gpu.cts) on SDL_GPU, on whatever device this machine has (lavapipe in
// Linux CI): the same batches over the same frame as the CPU reference (mrender.particle). With
// OMUI_REQUIRE_GPU=1 (CI) a missing device fails; otherwise it is said and the case passes vacuously.
import std;
import mlibs.rhi;
import mlibs.cts;
import mrender.rhi.sdl;
import mrender.particle.gpu.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"particle.gpu.sdl", [] {
    "the particles' suite passes on this device, every case"_test = [] {
        mrender::rhi::sdl::Options options;
        options.formats = mrender::particle::gpu::cts::shader_formats();
        options = environment_options(std::move(options));  // OMUI_VULKAN_LIBRARY, OMUI_GPU_VIDEO_DRIVER
        auto device = mrender::rhi::sdl::create(options);
        if (!device) {
            if (mrender::rhi::sdl::device_required()) expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", device.error().message);
            else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", device.error().message);
            return;
        }
        expect(eq(mrender::particle::gpu::cts::cases().size(), std::size_t{2}));
        mrender::particle::gpu::cts::declare(**device, {.subject = (*device)->caps().device});
    };
}};
