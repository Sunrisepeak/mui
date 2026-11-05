# xengine.render

The rendering architecture (module `xengine.render`, D-81). A frame is a graph of passes over the device's textures; every rendering feature records its passes into it, so 2D and 3D share one device, one texture pool, one order and one trace.

- **The pool** (`:pool`). Transient textures by what they are — size, format, usage. A frame asks, the pool hands out one it already made when one is free and makes one when none is; at the next frame they are all free again, and one nobody asked for in `keep` frames (120) is destroyed. A steady interface makes no textures from frame to frame.
- **The graph** (`:graph`). A pass names what it reads and writes and records its commands when the graph is recorded. Before anything is recorded the graph is checked and planned: a pass that reads a transient texture no earlier pass wrote is a problem naming both; a pass whose writes reach neither an output, nor a texture from outside the graph, nor a pass that runs after it is left out (a layer nobody composites costs nothing); what is left runs in the order it was described — passes are never reordered, so features compose by the order they add their passes — each in a trace span of its name (category `render`).

```cpp
namespace render = xengine::render;
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

## Independent boundary and compatibility

This package is the shared device-pass and texture-pool contract. It can record
custom texture operations without a display list, font system, vector rasterizer
or UI; graph device CTS exercise that task directly. Vector rendering consumes
this contract, and further rendering features must record into the same graph
rather than create another resource scheduler. Keeping this small library
independent excludes vector/hanim/raster/shader generation from graph-only users.

`import xengine.render;` uses `xengine::render`. The same package retains
`import openmui.render;` and the `omui::render` namespace alias. Rebuild callers;
namespace aliases cannot be reopened. There is one graph/pool implementation,
not two runtime owners. CLI selectors and dependency keys use the new package.
