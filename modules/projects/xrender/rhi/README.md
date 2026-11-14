# xrender/rhi

xrender's implementations of the GPU abstraction [mlibs.rhi](../../../gfx/rhi): the abstract interface stays in mlibs, because renderer and computation share it; the backends are the renderer's (plan §7). A host chooses one and hands it, as an `mlibs::rhi::Device`, to rendering, simulation and the UI.

| Package | |
|---|---|
| [sdl](sdl) | `xrender.rhi.sdl`: `mlibs.rhi` on SDL_GPU — Vulkan, Metal, Direct3D 12 |
| [webgpu](webgpu) | `xrender.rhi.webgpu`: on the page's WebGPU — the Web's main path, WGSL shaders |
| [webgl](webgl) | `xrender.rhi.webgl`: on the page's WebGL2 — the Web's compatibility path, ESSL shaders, no compute |

Each runs mlibs.rhi's conformance suite ([cts](../../../gfx/rhi/cts)).
