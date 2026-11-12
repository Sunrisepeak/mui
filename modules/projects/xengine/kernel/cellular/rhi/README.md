# xengine.cellular.rhi

An optional RHI compute kernel for `xengine::cellular::Program`. One Slang source is compiled by the existing shader build rule into the selected platform's formats. Native SPIR-V and browser WGSL execute the same validated integer descriptor; there is no shader authored per scenario.

```cpp
auto kernel = xengine::cellular::gpu::Kernel::prepare(device, program);
if (!kernel) { report(kernel.error()); return; }
auto recorded = (*kernel)->record(commands, input_field, output_field, boundary);
if (!recorded) { report(recorded.error()); return; }
auto submitted = device.submit(commands); // host handles fence, failure and publication
```

Each `Field` borrows an explicit device, buffer, shape and channel count. Caller-owned input/output buffers must be disjoint, with compute-read/write usages and sufficient size. Record appends one full-field dispatch (8×8 workgroups), with validation before mutation. It does not submit, wait, read back, allocate simulation state or advance time. Multiple ticks can stay resident and be recorded with rendering/other compute on the shared command list. [CONTRACT.md](CONTRACT.md) defines the lifetime and failure boundaries.

Minimal SOP: prepare CPU rules; query the target device and `shader_formats`; create the kernel and two fields; upload an initial state; record complete steps and swap logical field views; submit on the same queue; observe fences/errors before publishing; use explicit readback/upload for checkpoints. Compare **intermediate states**, edge modes and replay against CPU before accepting a backend. Permission, high-level operations, cancellation and revision belong to the host's Agent adapter, not individual GPU invocations. No GUI/window is required for a supported offscreen device.

| Package | Role |
|---|---|
| [cts](cts) | Same backend-neutral scenarios on actual SDL_GPU and WebGPU devices |

The existing backend tests run the domain CTS alongside RHI CTS. Use `OMUI_REQUIRE_GPU=1 OMUI_GPU_VIDEO_DRIVER=offscreen mcpp test --locked -p xrender.rhi.sdl` on Linux. `tools/ci/browser.sh` builds/runs WebGPU tests in Chromium; a standalone Node run is not GPU validation. Missing compute/readback is not a successful domain CTS run. GPU timings are not yet exposed by RHI; tests do not establish hardware speedup. Trace category `simulation` includes preparation/record spans; no per-cell host logging or callbacks.

## Independent boundary and compatibility

This adapter is separately selectable because it adds RHI and host shader
compilation to the CPU rule contract. It borrows device resources and records
commands; it does not own a second clock, rule evaluator or world. The CTS
package is separately selected by the SDL/WebGPU backend tests and does not
enter an ordinary adapter consumer.

Use `xengine.cellular.rhi` and `xengine::cellular::gpu`.
The old module and `omui::simulation::cellular::gpu` names forward through the
same CPU parent alias and the same implementation. The unchanged Slang kernel
is generated as a partition of `xengine.cellular.rhi.api`.

For local Linux validation with multiple installed Mesa stacks, select one
Vulkan loader and ICD as `tools/ci/gpu.sh` does before running required-device
tests. A mixed LLVM 20/22 process was observed to crash during driver JIT;
using the paired loader/lavapipe ICD avoids mixing those libraries. This is
validation environment setup, not a skipped GPU test or an engine fallback.

## Painting the field (D-132)

`Painter` writes a field's pixels on the device: each cell's pixel by the first swatch it passes (`pixel_of`, the cellular core's rule, shared with the CPU presenter), into an rgba8 texture with `compute_write` usage — what a renderer draws as an external texture, without the field ever coming back to the CPU. Its palette is uploaded once (`prepare`); `record` appends one compute pass and refuses (appending nothing) a field of another device, the wrong channels, or a texture of the wrong size or usage. The CTS case `cellular.paint` compares every cell's pixel with the CPU's rule over a field stepped on both sides, bit for bit.
