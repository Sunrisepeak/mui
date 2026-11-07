# xrender.cts

The render contract's conformance suite, on `mlibs.cts` (convergence plan §5.5, D-118). It belongs to xrender, the contract's owner, and every backend's tests run it on their device: `xrender.rhi.sdl`'s `render_cts_test` on SDL_GPU (lavapipe in Linux CI, Metal on macOS, Vulkan/lavapipe on Windows), writing `sdl.render.cts.omui.json` beside its scratch files.

```cpp
import xrender.cts;
auto report = xrender::cts::run(device, {.subject = device.caps().device, .platform = "sdl"}, {}, {.scratch = dir});
check(report.ok(), report.text());
```

Make the device with `xrender::cts::shader_formats()` (the formats the renderer's pipelines are compiled to). 47 cases, each with a stable id under the contract `xrender@1`:

- `render.graph.*` — a layer made and copied to the target; what nothing reads is culled; the next frame reuses the pool (level S).
- `render.vector.scene.*` — one case a scene (rectangles, gradients, rounded fills and strokes, paths and dashes, shadows, clips, transforms, layers, every blend mode and its arithmetic, every built-in effect, liquid glass, images, meshes, lit meshes, glyphs): the GPU's pixels against the CPU reference's within the scene's tolerance (level B: the largest channel difference, the pixels off by more than one).
- `render.vector.pixelate-dpi`, `render.vector.opacity-dpi` — effect units and identities at 1, 1.25 and 2 device pixels a logical one.
- `render.vector.mask-atlas` — masks kept from frame to frame, the atlas grown and emptied, every frame the CPU's pixels.
- `render.vector.graph-passes` — a frame is the render graph's: one trace span a pass, the same passes frame after frame (D-81).
- `render.vector.unsupported-counted`, `render.vector.image-upload-once`, `render.vector.external-texture`.
- `render.paint-mix.*` — gradient and lit image fields mixed under a shared path and clip; sixteen translucent leaves covered once.
- `render.effect.content-scopes`, `render.effect.output-bounds` — nested filter scopes agree across capture modes and densities; glass changes nothing outside its declared output reach.

A failing comparison leaves both pictures (`*.cpu.png`, `*.gpu.png`) in the runner's scratch directory and says where. Lavapipe is software Vulkan and is reported as such: no case is evidence of physical-GPU performance. Portable CPU tests and image hashes stay in `xrender.vector` and `xrender.raster`.
