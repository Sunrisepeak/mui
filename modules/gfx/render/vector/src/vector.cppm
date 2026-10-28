// openmui.render.vector — 2D vector rendering, a rendering feature of
// openmui.render (D-57, D-81).
//
//   :display    the display list: paths, paints, strokes, clips, transforms,
//               layers, glyph runs, images, node markers
//   :resources  images by reference, glyphs from the text engine
//   :damage     which rows a new list changes (incremental frames)
//   :cpu        the CPU reference backend over openmui.raster
//   :gpu        the GPU backend over omui.rhi (its shaders: :ui_shaders)
//
// Nothing in this package knows about nodes or views: anything that produces
// a display list can render it.
export module openmui.render.vector;

// The graph its GPU passes are recorded into.
export import openmui.render;

export import :display;
export import :resources;
export import :damage;
export import :cpu;
export import :gpu;
export import :effects;
