// openmui.rhi.cts — what every omui.rhi backend must render and compute, and
// read back, exactly (plan §15, level B). A case builds its resources on the
// device given, records a list, submits it, reads the result back and
// compares it with what an exact device produces: 8-bit colours that are
// multiples of 1/5 convert exactly, so only blending allows ±1.
//
//   const auto report = mlibs::rhi::cts::run(device, {.subject = "lavapipe"});
//   check(report.ok(), report.text());   // report.json(): mlibs.cts's machine-readable report
//
// The shaders are the suite's own (shaders/*.slang), compiled at build time
// for the target's formats; shader_formats() says which, so a backend can
// open a device that takes one.
export module mlibs.rhi.cts.api;

import std;
import mlibs.rhi;
export import mlibs.cts;
export import :fill_shaders;
export import :textured_shaders;
export import :compute_shaders;
export import :depth_shaders;

export namespace mlibs::rhi::cts {

// Every case, in order, on `device` (ids `rhi.<case>`, contract mlibs.rhi@1):
// a case's first difference is its failure; what a case that holds says of
// how (a device without compute refusing it) is its evidence.
[[nodiscard]] mlibs::cts::Report run(Device& device, mlibs::cts::Environment environment = {});

// The names of the cases (without the `rhi.` of their ids), in the order
// run() runs them.
[[nodiscard]] std::vector<std::string_view> cases();

// The shader formats the suite's shaders were compiled to by this build.
[[nodiscard]] ShaderFormats shader_formats() noexcept;

}  // namespace mlibs::rhi::cts
