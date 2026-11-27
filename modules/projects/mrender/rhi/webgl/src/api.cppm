// openmui.rhi.webgl — omui.rhi on the page's WebGL2 (D-84 §6.3: the Web's
// compatibility path, for the browsers and WebViews without WebGPU — Toy's
// among them, Q-22). Shaders are ESSL (GLSL ES 3.00), which openmui.shader
// writes from Slang for a wasm32-emscripten build.
//
//     auto device = mrender::rhi::webgl::create({.canvas = "#canvas"});
//     if (!device) fall_back(device.error().message);   // Errc::unsupported: no WebGL2 in this page
//
// The translation is the page's (src/js/omui_webgl.js): a resource is one
// call into it, a submission one more — the list as rhi's wire words, which
// become GL calls in its order. WebGL2 is synchronous, so wait() has nothing
// to wait for and read() reads back in any program. What WebGL2 lacks is
// said up front: no compute (Caps::compute false: a compute pipeline is
// refused as unsupported) and no storage textures or buffers (refused when
// made); multisampled targets are renderbuffers, resolved when their pass
// ends. A context is the canvas's that present() shows frames on, or one of
// the page's own that nobody sees; one device lives in a page at a time.
// Off the Web there is no WebGL2, and create() says so.
export module mrender.rhi.webgl.api;

import std;
import mlibs.rhi;

namespace mrender::rhi::webgl { using namespace mlibs::rhi; }  // the interface it implements
export namespace mrender::rhi::webgl {

struct Options {
    // The canvas (a CSS selector; Module.canvas when the page set one) whose
    // WebGL2 context the device is, and that present() shows frames on.
    // Empty: a canvas of the page's own that nobody sees (offscreen work).
    std::string canvas;
    // GL's errors read after every submission and present: each a round trip to the browser's GPU process, which
    // waits for the work before it (a debugging session, a conformance run). Off, they are read where that costs
    // nothing more or where an error comes from — a resource made, a readback, wait(), the first frames and the
    // next ones after anything is made — and an error is still said, by the first of those after it (GL keeps it
    // until it is read): what is lost is which submission made it. A page turns it on with ?gl-checks.
    bool checked = false;
};

// A device, or why there is none (Errc::unsupported: not in a page, no
// WebGL2 — the caller falls back to the CPU renderer).
[[nodiscard]] Result<std::unique_ptr<Device>> create(const Options& options = {});

}  // namespace mrender::rhi::webgl
