// openmui.render — the rendering abstraction (D-57).
//
//   :display    the display list: paths, paints, strokes, clips, transforms,
//               layers, glyph runs, images, node markers
//   :resources  images by reference, glyphs from the text engine
//   :damage     which rows a new list changes (incremental frames)
//   :cpu        the CPU reference backend over openmui.raster
//
// Nothing in this package knows about nodes or views: anything that produces
// a display list can render it.
export module openmui.render;

export import :display;
export import :resources;
export import :damage;
export import :cpu;
export import :effects;
