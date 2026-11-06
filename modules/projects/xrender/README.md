# xrender

A CPU/GPU same-source, Web-first renderer whose pixels agents can review (internal code name; plan §7 asks for another public name before the API freezes, because of X11's XRender).

**Signature features — candidates** (plan §9.5; none is claimed outside the repository before its evidence exists):

| Feature | Kind | Evidence needed |
|---|---|---|
| CPU and GPU backends whose pixels are compared, with a deterministic CPU reference | leading | CPU/GPU difference reports; quality and throughput against Skia, Vello, ThorVG |
| Web first: a submission crosses the Wasm boundary as one compact byte stream (one call a submission, not one a draw) | leading | CPU overhead against comparable Web renderers |
| Region pixels with their provenance, for agents to review | for what comes next | an agent review task |

| Package | |
|---|---|
| [graph](graph) | `xrender.graph`: the render graph and the texture pool (below) |
| [vector](vector) | 2D vector rendering: the display list, its CPU reference renderer and its GPU renderer |
| [effect](effect) | A program's own pixel effects (`*.effect.omui.toml` and Slang), loaded and reloaded while it runs |
| [text](text) | `xrender.text`: mlibs.text's glyphs rasterised and cached, its paragraphs painted (layer content) |
| [raster](raster) | The deterministic CPU rasteriser: paths, strokes, gradients, images, clips, layers, blend modes, shadows |
| [rhi](rhi) | The backends of `mlibs.rhi`: SDL_GPU, WebGPU, WebGL2 |
| [cts](cts) | Desktop graph and vector device conformance |

The general pieces stay in mlibs: the GPU interface [mlibs.rhi](../../gfx/rhi) and the shader toolchain [mlibs.shader](../../gfx/shader), shared with GPU computation.

## The render graph

The rendering architecture (package and module `xrender.graph`, namespace `xrender`, D-81). A frame is a graph of passes over the device's textures; every rendering feature records its passes into it, so 2D and 3D share one device, one texture pool, one order and one trace.

- **The pool** (`:pool`). Transient textures by what they are — size, format, usage. A frame asks, the pool hands out one it already made when one is free and makes one when none is; at the next frame they are all free again, and one nobody asked for in `keep` frames (120) is destroyed. A steady interface makes no textures from frame to frame.
- **The graph** (`:graph`). A pass names what it reads and writes and records its commands when the graph is recorded. Before anything is recorded the graph is checked and planned: a pass that reads a transient texture no earlier pass wrote is a problem naming both; a pass whose writes reach neither an output, nor a texture from outside the graph, nor a pass that runs after it is left out (a layer nobody composites costs nothing); what is left runs in the order it was described — passes are never reordered, so features compose by the order they add their passes — each in a trace span of its name (category `render`).

```cpp
namespace render = xrender;
namespace rhi = mlibs::rhi;
render::TexturePool pool(device);
render::Graph graph(pool);
auto layer = graph.transient({w, h, rhi::Format::rgba8_unorm, color_target | sampled}, "layer");
graph.pass({.name = "ui.layer", .writes = {*layer}}, [&](rhi::CommandList& l) { /* draw into it */ });
graph.pass({.name = "ui.composite", .reads = {*layer}, .writes = {target}}, [&](rhi::CommandList& l) { /* composite */ });
auto report = graph.record(list, {&target, 1});   // recorded, culled, transients
```

`tests/graph_test.cpp` holds the portable pool reuse/release, error wording,
planning and trace cases on a recording device. The actual desktop-device
bytes remain checked in [cts](cts), whose SDL dependency stays outside this
portable runtime package. Both this package and its serial tests run on Web.

## The rendering features

| Package | |
|---|---|
| [vector](vector) | 2D vector rendering: the display list, its CPU reference renderer and its GPU renderer, whose passes are this graph's |
| [effect](effect) | A program's own pixel effects (`*.effect.omui.toml` and Slang), loaded and reloaded while it runs |
| [cts](cts) | Desktop-only graph/vector device conformance; native workspace CI includes it, Web CI keeps the portable CPU suites |

Depends on: `mlibs.rhi`, `mlibs.trace`.

## Independent boundary

This package is the shared device-pass and texture-pool contract. It can record
custom texture operations without a display list, font system, vector rasterizer
or UI; graph device CTS exercise that task directly. Vector rendering consumes
this contract, and further rendering features must record into the same graph
rather than create another resource scheduler. Keeping this small library
independent excludes vector/hanim/raster/shader generation from graph-only users.

`import xrender.graph;` gives namespace `xrender`: one graph and pool
implementation, not two runtime owners.
