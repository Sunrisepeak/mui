// xrender.cts — the render contract's conformance suite (convergence plan
// §5.5), on mlibs.cts: what a GPU device draws through xrender's renderer,
// against the CPU reference pixel for pixel within each case's tolerance —
// scenes of every kind of draw, effects, paint mixtures, the render graph's
// passes and culling, the mask atlas, image uploads, external textures,
// effect output bounds at several pixel densities. The owner's suite: every
// backend's tests run it on their device (xrender.rhi.sdl's on SDL_GPU).
//
//     xrender::cts::declare(device, {.subject = device.caps().device}, {.scratch = dir});
//     // mtest's conformance cases: <device>/render.graph.layers-and-culling, …
export module xrender.cts;

import std;
import mlibs.rhi;
import mlibs.cts;

export namespace xrender::cts {

struct Options {
    // Where a failing comparison leaves both pictures (empty: nowhere).
    std::filesystem::path scratch;
};

// The shader formats the renderer's pipelines are compiled to: what a device
// for this suite is made with.
[[nodiscard]] mlibs::rhi::ShaderFormats shader_formats();

// Every case's id, in order.
[[nodiscard]] std::vector<std::string> cases();

// Every case against `device` (contract xrender@1): mtest's conformance cases `<device>/render.*` (in a
// tally, counted).
void declare(mlibs::rhi::Device& device, mlibs::cts::Against who = {}, Options options = {},
             std::source_location at = std::source_location::current());

}  // namespace xrender::cts
