// openmui.rhi.cts — what every omui.rhi backend must render and compute, and
// read back, exactly (plan §15, level B). A case builds its resources on the
// device given, records a list, submits it, reads the result back and
// compares it with what an exact device produces: 8-bit colours that are
// multiples of 1/5 convert exactly, so only blending allows ±1.
//
//   for (const auto& o : omui::rhi::cts::run(device)) check(o.passed, o.name + ": " + o.detail);
//
// The shaders are the suite's own (shaders/*.slang), compiled at build time
// for the target's formats; shader_formats() says which, so a backend can
// open a device that takes one.
export module openmui.rhi.cts;

import std;
import openmui.rhi;
export import :fill_shaders;
export import :textured_shaders;
export import :compute_shaders;
export import :depth_shaders;

export namespace omui::rhi::cts {

struct Outcome {
    std::string name;
    bool        passed = false;
    std::string detail;  // the first difference, or why it could not run
};

// Every case, in order, on `device`.
[[nodiscard]] std::vector<Outcome> run(Device& device);

// The names of the cases, in the order run() runs them.
[[nodiscard]] std::vector<std::string_view> cases();

// The shader formats the suite's shaders were compiled to by this build.
[[nodiscard]] ShaderFormats shader_formats() noexcept;

}  // namespace omui::rhi::cts
