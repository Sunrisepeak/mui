# openmui.rhi.cts

The conformance suite every [omui.rhi](..) backend runs (level B, plan §15). Each case builds its resources on the device it is given, records a command list, submits it, reads the result back, and compares it with what an exact device produces. 8-bit colours that are multiples of 1/5 convert exactly, so only blending and a resolved edge allow ±1.

```cpp
import openmui.rhi.cts;

auto device = omui::rhi::sdl::create({.formats = omui::rhi::cts::shader_formats()});
for (const auto& o : omui::rhi::cts::run(**device)) check(o.passed, o.name + ": " + o.detail);
```

| Case | What must hold |
|---|---|
| `clear` | a clear colour, byte for byte |
| `orientation` | NDC +y is the top row and x −1 the left column, on every backend |
| `instancing` | a per-instance vertex buffer moves four copies of a quad |
| `firsts` | a draw from its first vertex and its first instance: the right vertices, moved by the right offsets |
| `indexed` | 16-bit and 32-bit indices, a first index and a vertex offset |
| `blending` | premultiplied source-over (±1) |
| `scissor`, `viewport` | only the rectangle given is drawn |
| `texture_nearest` | an uploaded texture sampled nearest: uv (0, 0) is the top-left texel |
| `uniforms` | a fragment uniform multiplies the texture |
| `load_keeps` | a pass that loads keeps the previous pass's pixels |
| `copy` | a texture region copied to another position |
| `compute` | a compute pass writes every texel of a storage texture and every element of a storage buffer, its size from uniforms; a device that says it has no compute (`Caps::compute`, WebGL2) refuses the pipeline as `unsupported` (the outcome's note says so) |
| `formats` | `r8_unorm` uploaded and `rgba16_float` cleared, read back byte for byte |
| `ordering` | a submission sees what an earlier one wrote |
| `buffers` | a buffer upload at an offset, read back |
| `depth_test` | the depth test (`less`): a quad drawn behind another is not drawn, one in front is, whatever the order |
| `depth_kept` | depths a pass stores are loaded and tested by the next pass |
| `multisample` | four samples a texel, resolved: a texel half inside an edge is half covered (128 ±1) — the standard 4× positions, the same on Vulkan, Metal and Direct3D 12; every backend must take 4 |

A failure names the first difference (`pixel (0, 0) is (255, 0, 0, 255); it should be (0, 0, 0, 255)`).

The shaders are the suite's own (`shaders/*.slang`: `fill`, `textured`, `compute`, `depth`). [openmui.shader.rules](../../shader/rules) compiles them at build time for the target's formats (WGSL on the Web), so the package exports them as `omui::rhi::cts::shaders::*` constants, and `shader_formats()` says which formats a device must take. [openmui.rhi.sdl](../sdl)'s tests run the suite on lavapipe (Linux, Windows) and Metal (macOS) in CI; [openmui.rhi.webgpu](../webgpu)'s run it on the page's WebGPU in headless Chromium (`tools/ci/browser.sh`).
