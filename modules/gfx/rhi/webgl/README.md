# openmui.rhi.webgl

[omui.rhi](..) on the page's WebGL2: the Web's compatibility path, for the browsers and WebViews without WebGPU — Bilibili Toy's among them (D-84 §6.3, Q-22). Shaders are ESSL (GLSL ES 3.00), which [openmui.shader](../../shader) writes from Slang for a `wasm32-emscripten` build.

```cpp
import openmui.rhi;
import openmui.rhi.webgl;

auto device = omui::rhi::webgl::create({.canvas = "#canvas"});
if (!device) fall_back(device.error().message);   // Errc::unsupported: no WebGL2 in this page
```

- **The context is made with the device**: the canvas `Options::canvas` names (`Module.canvas` when the page set one), which `present()` shows frames on, or a canvas of the page's own that nobody sees. No depth or stencil buffer of its own, no antialiasing: rhi's targets are textures. `Caps` names the renderer (`WEBGL_debug_renderer_info` where the browser tells it) and says whether it is a software one.
- **One call into the page a resource, one a submission.** A validated list crosses as rhi's wire words (`openmui.rhi:wire`, as for [rhi.webgpu](../webgpu)) and becomes GL calls in its order. The state rhi keeps per pass and pipeline is the page's to keep: a framebuffer per colour and depth target, cleared as the pass begins with the scissor off; a pipeline's program, blend and depth state set with it; vertex attributes pointed at the bound buffers when a draw finds them changed; textures and samplers on units (a fragment stage's slot *i* on unit *i*, a vertex stage's on 16 + *i*); uniforms written once a submission into one buffer at the context's offset alignment, each bound by range (a vertex stage's slot *i* at point *i*, a fragment stage's at 4 + *i*), covering the block the program declares.
- **What WebGL2 lacks is emulated or refused up front.** A draw's vertex offset and first instance move into the attribute pointers (WebGL2 has neither), and openmui.shader's `omui_base` uniform gives Vulkan's draw parameters to a shader that reads them. There is no compute (`Caps::compute` false: a compute pipeline is `Errc::unsupported`) and no storage textures or buffers (refused when made); a buffer is indices or anything else, never both (WebGL2's rule). Multisampled targets are renderbuffers, resolved by a blit when their pass ends. Float colour targets need `EXT_color_buffer_float` (`rgba16_float` takes the half-float extension too).
- **Rows are top-down, as on every backend.** GL's are bottom-up, so openmui.shader's ESSL flips y at the vertex stage's end: GL's row 0 is the top too, viewports and scissors are rhi's as they are, `gl_FragCoord` is `SV_Position`, and only `present()` flips (the canvas is GL's). Depth is mapped from rhi's 0–1 to GL's −1–1 there as well.
- **bgra8 is RGBA8 underneath** (WebGL2 has no BGRA): its bytes are swapped where they cross, uploads and readback, so a caller sees them as given.
- **Synchronous**: `wait()` has nothing to wait for and `read()` reads back in any program (no suspension needed, unlike WebGPU). GL's errors are looked at once a submission and said by that call, or the next (`Errc::backend`; a lost context `Errc::device_lost`).
- One device lives in a page at a time. Off the Web (`src/native/elsewhere`) there is no WebGL2, and `create()` says so.

The tests run three ways:

- `tests/webgl_test.cpp`, natively and under node in the Web row: no WebGL2 there, so `create()` answers `Errc::unsupported`, and the cases that need a device say so (`OMUI_REQUIRE_WEBGL=1` makes that a failure).
- `tests/js/webgl.test.mjs`, the page's half under node with a fake WebGL2 context (`fake_gl.mjs`) that remembers what it is asked: rhi's enumerations and the wire's ops as the C++ declares them, the context and the canvas, buffers by their kind, textures and renderbuffers, a program's units and block points, a submission's order (uniforms, framebuffers, clears, pipeline state, attribute pointers with the vertex offset and first instance moved in, units, ranges, draws), copies and resolves reading one framebuffer and drawing another, bgra8's swaps, readback of every format, errors and a lost context, presenting flipped.
- `tools/ci/browser.sh`: the same `webgl_test.cpp` in headless Chromium — the page's real WebGL2 (ANGLE on SwiftShader on a machine with no GPU) runs every case of the [conformance suite](../cts), `compute` as the refusal it must be — and every ESSL entry point the Web build wrote compiled and linked by the browser.
