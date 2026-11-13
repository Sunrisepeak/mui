# xrender

A CPU/GPU same-source, Web-first renderer whose pixels agents can review (internal code name; plan §7 asks for another public name before the API freezes, because of X11's XRender).

**Signature features — candidates** (plan §9.5; none is claimed outside the repository before its evidence exists):

| Feature | Kind | Evidence needed |
|---|---|---|
| CPU and GPU backends whose pixels are compared, with a deterministic CPU reference | leading | CPU/GPU difference reports; quality and throughput against Skia, Vello, ThorVG. First measured (2026-11-07, [tests/bench/competitors](../../../tests/bench/competitors)): the CPU reference is as close to Skia as resvg is, faster than Skia only on many small draws and 1.6–2.9× slower on large fills, gradients and strokes — quality holds, throughput does not lead yet |
| Web first: a submission crosses the Wasm boundary as one compact byte stream (one call a submission, not one a draw) | leading | CPU overhead against comparable Web renderers |
| Region pixels with their provenance, for agents to review | for what comes next | an agent review task. Started: a display list as SVG ([vector](vector), `xrender.vector.svg`) — every piece a vector or a marked island of the reference's pixels, every node's drawing marked; omui's `capture` gives it to agents |

| Package | |
|---|---|
| [graph](graph) | `xrender.graph`: the render graph and the texture pool (below) |
| [vector](vector) | 2D vector rendering: the display list, its CPU reference renderer and its GPU renderer |
| [scene](scene) | `xrender.scene`: what there is to draw in three dimensions and what a view of it sees — instances and lights by stable id, a change list a mirror follows, deterministic culling, sorting and batching; no device |
| [mesh](mesh) | `xrender.mesh`: meshes drawn, the CPU reference — colour, depth, normal, velocity and an id buffer down to the triangle, the same bits everywhere; what the GPU path is compared with |
| [mesh/gpu](mesh/gpu) | The mesh pass on a device through `mlibs.rhi`: one Slang shader, held to the CPU reference pixel by pixel (5 of 129,600 pixels apart on lavapipe) |
| [sprite](sprite) | Pictures on bones in 2D: a region skinned over a grid of cells to a bone and the bones it bends to (gongfu's fighters are its consumer); batching and atlases to come |
| [frame](frame) | `xrender.frame`: the frame's entry — layers composed into a target, captured, and traced from a pixel to what drew it; the frame as an agent's subject |
| [effect](effect) | A program's own pixel effects (`*.effect.omui.ts` and Slang), loaded and reloaded while it runs |
| [text](text) | `xrender.text`: mlibs.text's glyphs rasterised and cached, its paragraphs painted (layer content) |
| [raster](raster) | The deterministic CPU rasteriser: paths, strokes, gradients, images, clips, layers, blend modes, shadows |
| [rhi](rhi) | The backends of `mlibs.rhi`: SDL_GPU, WebGPU, WebGL2 |
| [cts](cts) | Desktop graph and vector device conformance |
| [tool](tool) | The `xrender` command: effects checked and previewed by the GPU renderer, the conformance suite and the device, headless (its operations' catalog and skill) |

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
| [effect](effect) | A program's own pixel effects (`*.effect.omui.ts` and Slang), loaded and reloaded while it runs |
| [cts](cts) | The render contract's suite on mlibs.cts (48 cases), run by each backend's tests (D-118) |

Depends on: `mlibs.rhi`, `mlibs.trace`.

Planned with hanim's representations (convergence plan §6.8, M3.5): strokes trimmed by arc length (a start, an end and an offset that animate) as a drawing capability of the vector renderer, CPU and GPU alike; glyph outlines handed to hanim's glyph adapter as `mlibs.geom` paths.

## Independent boundary

This package is the shared device-pass and texture-pool contract. It can record
custom texture operations without a display list, font system, vector rasterizer
or UI; graph device CTS exercise that task directly. Vector rendering consumes
this contract, and further rendering features must record into the same graph
rather than create another resource scheduler. Keeping this small library
independent excludes vector/hanim/raster/shader generation from graph-only users.

`import xrender.graph;` gives namespace `xrender`: one graph and pool
implementation, not two runtime owners.
