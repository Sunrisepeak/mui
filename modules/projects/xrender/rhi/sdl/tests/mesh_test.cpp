// The mesh pass's contract (xrender.mesh.gpu.cts) on SDL_GPU, on whatever device this machine has (lavapipe in
// Linux CI): the same scenes as the CPU reference (xrender.mesh), each within the tolerance its case says. With
// OMUI_REQUIRE_GPU=1 (CI) a missing device fails; otherwise it is said and the case passes vacuously. Each
// case's picture of the device is kept in the test's scratch directory.
import std;
import mlibs.rhi;
import mlibs.cts;
import xrender.rhi.sdl;
import xrender.mesh.gpu.cts;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;

UTest cases{"mesh.gpu.sdl", [] {
    "the mesh pass's suite passes on this device, every case"_test = [] {
        xrender::rhi::sdl::Options options;
        options.formats = xrender::mesh::gpu::cts::shader_formats();
        options = environment_options(std::move(options));  // OMUI_VULKAN_LIBRARY, OMUI_GPU_VIDEO_DRIVER
        auto device = xrender::rhi::sdl::create(options);
        if (!device) {
            if (xrender::rhi::sdl::device_required()) expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", device.error().message);
            else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", device.error().message);
            return;
        }
        expect(eq(xrender::mesh::gpu::cts::cases().size(), std::size_t{21}));
        // The suite's cases, this program's own: <device>/mesh.yard, …
        xrender::mesh::gpu::cts::declare(**device, {.subject = (*device)->caps().device}, {.scratch = scratch_dir("xrender-mesh-gpu")});
    };
}};
