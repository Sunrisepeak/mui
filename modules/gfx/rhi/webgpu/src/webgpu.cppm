// openmui.rhi.webgpu — omui.rhi on the page's WebGPU (D-84 §6.3: the Web's
// main path; architecture §8.2). Shaders are WGSL, which openmui.shader
// writes from Slang for a wasm32-emscripten build.
//
//     auto device = omui::rhi::webgpu::create({.canvas = "#canvas"});
//     if (!device) fall_back(device.error().message);   // Errc::unsupported: no WebGPU in this page
//
// The page asks for its device as the program loads (main() starts once it
// has one, or knows it has none), so create() answers at once. The
// translation is the page's (src/js/omui_webgpu.js): a resource is one call
// into it, a submission one more — the list as rhi's wire words, which
// become one command encoder. Off the Web, and in a page without WebGPU,
// create() answers Errc::unsupported.
//
// WebGPU waits asynchronously. In a program linked to suspend (-sASYNCIFY,
// or -sJSPI) wait() waits for the GPU and read() reads back; in any other,
// wait() returns at once — what a submission reads is copied when it is
// submitted and the queue runs everything in order, so there is nothing a
// caller could see sooner — and read() answers Errc::unsupported. WebGPU
// reports a mistake asynchronously too: the next submit, wait or read says
// it (Errc::backend). One device lives in a page at a time.
export module openmui.rhi.webgpu;

import std;
import openmui.rhi;

export namespace omui::rhi::webgpu {

struct Options {
    // The canvas (a CSS selector; Module.canvas when the page set one) that
    // present() shows frames on, configured for the device. Empty: none
    // (offscreen work only).
    std::string canvas;
};

// A device, or why there is none (Errc::unsupported: not in a page, no
// WebGPU, no adapter — the caller falls back to WebGL2 or the CPU).
[[nodiscard]] Result<std::unique_ptr<Device>> create(const Options& options = {});

// Whether the page has a WebGPU device for the program.
[[nodiscard]] bool available();

// Whether this program was linked to wait for WebGPU (-sASYNCIFY or
// -sJSPI): what wait() and read() need in a page.
[[nodiscard]] bool can_wait();

}  // namespace omui::rhi::webgpu
