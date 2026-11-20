// xrender.particle.gpu.cts — particles drawn on a device (xrender.particle.gpu@1): what any backend of
// `mlibs.rhi` must draw of the same batches over the same frame, held to the CPU reference (`xrender.particle`)
// within tolerances said case by case. A backend's tests open their device and declare the suite against it:
//
//     options.formats = xrender::particle::gpu::cts::shader_formats();
//     auto device = backend::create(options);
//     xrender::particle::gpu::cts::declare(**device, {.subject = (*device)->caps().device});
export module xrender.particle.gpu.cts;

import std;
import mlibs.rhi;
import mlibs.cts;

export namespace xrender::particle::gpu::cts {

// The shader formats the particles' and the meshes' shaders are compiled to: what a device for this suite is made with.
[[nodiscard]] mlibs::rhi::ShaderFormats shader_formats();
// Every case's id, in order.
[[nodiscard]] std::vector<std::string> cases();
// Every case against `device`: mtest's conformance cases `<device>/particle.*`.
void declare(mlibs::rhi::Device& device, mlibs::cts::Against who = {}, std::source_location at = std::source_location::current());

}  // namespace xrender::particle::gpu::cts
