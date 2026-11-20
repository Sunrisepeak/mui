// xrender.mesh.gpu.cts — the mesh pass's contract on a device (xrender.mesh.gpu@1): what any backend of
// `mlibs.rhi` must draw of the same scenes, held to the CPU reference (`xrender.mesh`) within tolerances said
// case by case. A backend's tests open their device and declare the suite against it:
//
//     options.formats = xrender::mesh::gpu::cts::shader_formats();
//     auto device = backend::create(options);
//     xrender::mesh::gpu::cts::declare(**device, {.subject = (*device)->caps().device}, {.scratch = directory});
export module xrender.mesh.gpu.cts;

import std;
import mlibs.rhi;
import mlibs.cts;

export namespace xrender::mesh::gpu::cts {

struct Options {
    // Where each case's picture of the device is kept, for a person to look at (empty: nowhere).
    std::filesystem::path scratch;
};

// The shader formats the pass's shaders are compiled to: what a device for this suite is made with.
[[nodiscard]] mlibs::rhi::ShaderFormats shader_formats();

// Every case's id, in order.
[[nodiscard]] std::vector<std::string> cases();

// Every case against `device`: mtest's conformance cases `<device>/mesh.*` (in a tally, counted).
void declare(mlibs::rhi::Device& device, mlibs::cts::Against who = {}, Options options = {}, std::source_location at = std::source_location::current());

}  // namespace xrender::mesh::gpu::cts
