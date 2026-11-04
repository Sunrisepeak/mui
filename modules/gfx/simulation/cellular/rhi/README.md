# openmui.simulation.cellular.rhi

An optional RHI compute kernel for `omui::simulation::cellular::Program`. One Slang source is compiled by the existing shader build rule into the selected platform's formats. Native SPIR-V and browser WGSL execute the same validated integer descriptor; there is no shader authored per scenario.

```cpp
auto kernel = omui::simulation::cellular::gpu::Kernel::prepare(device, program);
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

The existing backend tests run the domain CTS alongside RHI CTS. Use `OMUI_REQUIRE_GPU=1 OMUI_GPU_VIDEO_DRIVER=offscreen mcpp test --locked -p openmui.rhi.sdl` on Linux. `tools/ci/browser.sh` builds/runs WebGPU tests in Chromium; a standalone Node run is not GPU validation. Missing compute/readback is not a successful domain CTS run. GPU timings are not yet exposed by RHI; tests do not establish hardware speedup. Trace category `simulation` includes preparation/record spans; no per-cell host logging or callbacks.
