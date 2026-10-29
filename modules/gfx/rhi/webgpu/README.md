# openmui.rhi.webgpu

[omui.rhi](..) on the page's WebGPU: the Web's main path (D-84 §6.3). Shaders are WGSL, which [openmui.shader](../../shader) writes from Slang for a `wasm32-emscripten` build.

```cpp
import openmui.rhi;
import openmui.rhi.webgpu;

auto device = omui::rhi::webgpu::create({.canvas = "#canvas"});
if (!device) fall_back(device.error().message);   // Errc::unsupported: no WebGPU in this page
```

- **The device is asked for as the program loads.** The page's half (`src/js/omui_webgpu.js`, an Emscripten library `build.mcpp` gives the link) requests the adapter and the device behind a run dependency, so `main()` starts once the page has one, or knows it has none, and `create()` answers at once. The page's `Module.omuiWebGpu.powerPreference` is passed to the adapter's request. `Caps` names the adapter and says whether it is a software one (SwiftShader).
- **One call into the page a resource, one a submission.** A validated list crosses as rhi's wire words (`openmui.rhi:wire`) and becomes one command encoder: every upload's bytes are staged in one buffer written before the encoder is submitted and copied in the list's order (texture rows at WebGPU's 256-byte pitch); uniforms go to another buffer at 256-byte offsets; bind groups are made when a draw or a dispatch finds what it binds changed.
- **Pipelines take WebGPU's `"auto"` layout**, from the shaders openmui.shader wrote: a stage's read-only group (the vertex stage's 0, the fragment stage's 2, the compute stage's 0) holds sampled texture *i* at binding 2*i* and its sampler at 2*i* + 1, then the storage textures and buffers; uniforms in the next group (1, 3; compute 2); a compute pass's outputs in group 1, the textures first.
- **What WebGPU cannot do is refused before the page sees it.** Buffers are written four bytes at a time, so an upload starts on a multiple of 4 and ends on one or at the buffer's end (`Errc::unsupported`, naming the command). Multisampling is 4 samples a texel (`Caps::max_samples`). Depth and stencil targets need the adapter's `depth32float-stencil8` (`Caps::depth`).
- **Waiting and reading back are asynchronous in WebGPU.** In a program linked to suspend (`-sASYNCIFY`, or `-sJSPI` where no static constructor reaches a `try` block) `wait()` waits for the queue and `read()` reads back. In any other, `wait()` returns at once — what a submission reads was copied when it was submitted, and the queue runs everything in order, so there is nothing a caller could see sooner — and `read()` answers `Errc::unsupported`. `can_wait()` says which. A mistake WebGPU reports (asynchronously) is said by the next submit, wait or read (`Errc::backend`; a lost device `Errc::device_lost`).
- **Presenting**: `Options::canvas` names the canvas (`Module.canvas` when the page set one), configured for the device in the browser's preferred format; `present(frame)` draws the frame over it, texel for pixel at the same size, filtered when scaled; false on a canvas of no size.
- One device lives in a page at a time. Off the Web (`src/native/elsewhere`) there is no WebGPU, and `create()` says so.

The tests run three ways:

- `tests/webgpu_test.cpp`, natively and under node in the Web row: no WebGPU there, so `create()` answers `Errc::unsupported`, and the cases that need a device say so (`OMUI_REQUIRE_WEBGPU=1` makes that a failure).
- `tests/js/webgpu.test.mjs`, the page's half under node with a fake WebGPU (`fake_gpu.mjs`) that remembers what it is asked: rhi's enumerations and the wire's ops as the C++ declares them, the device asked for as the program loads, the descriptors of resources and pipelines, a submission's copies, passes and bind groups in order, waiting and reading back with and without suspension, errors and a lost device, presenting.
- `tools/ci/browser.sh`: the same `webgpu_test.cpp` built with the `wasm-browser` profile (linked to suspend) in headless Chromium — the page's real WebGPU (SwiftShader on a machine with no GPU) runs every case of the [conformance suite](../cts) — and every WGSL entry point the Web build wrote compiled by the browser.
