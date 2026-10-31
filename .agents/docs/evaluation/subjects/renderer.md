# Subject: renderer — openmui's rendering

**What:** the render architecture (`modules/gfx/render`: render graph, texture pool), the 2D vector feature (`modules/gfx/render/vector`: display list, CPU renderer, GPU renderer, damage tracking, effects), the CPU rasteriser (`modules/gfx/raster`), the RHI and its backends (`modules/gfx/rhi`: Vulkan/Metal/D3D12 via SDL_GPU, WebGPU, WebGL2), shaders (`modules/gfx/shader`, Slang), effects (`modules/gfx/render/effect`). Read their READMEs, the structure-render design document, `tests/bench` (raster, frame) and the budgets.

**Top 3:** Skia (with Graphite) · Vello · Blend2D. **Reference:** ThorVG, Rive Renderer, Flutter Impeller.

**Dimensions:**
- `quality` — anti-aliasing, curves, strokes, gradients, image sampling, text rendering (hinting, subpixel, CJK), colour handling; pixel comparisons where available.
- `features` — paths, clips, layers, blend modes, filters and effects (blur, glass, glow), masks, text, images, video frames.
- `cpu_performance` — raster cost per pixel and per frame, damage, parallel bands; measured numbers vs Blend2D / Skia CPU.
- `gpu_performance` — GPU path: batching, atlases, render-graph scheduling; measured or provisional vs Vello / Skia Graphite / Impeller.
- `determinism` — the same bytes on every platform for the CPU path; GPU within stated tolerances; how proven.
- `architecture` — render graph, features sharing one graph, RHI abstraction, backends, shader pipeline.
- `portability` — backends and platforms, the Web, software fallbacks.
- `code_quality` — code, tests, conformance (RHI CTS), benchmarks.

**Run:**
- `omui-site --size 1440x900` (openmui.site)
- `omui-styles` (openmui.showcase.style_gallery)

**Use:** capture text, curves, gradients, images, glass and glow at rest and during motion (ui_clock step); zoom the site's atlas in and out and judge the image quality of each level; the frame meter's numbers on this machine's CPU path.

**Images:** attach pixel test pictures (effects, text) where they exist.
