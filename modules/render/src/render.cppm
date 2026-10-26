// openmui.render — the rendering abstraction (D-57).
//
//   :display   the display list: paths, paints, strokes, clips, transforms,
//              layers, glyph runs, images, node markers
//
// The renderer interface and its CPU (openmui-raster) and GPU (openmui-gpu)
// backends join here; nothing in this package knows about nodes or views.
export module openmui.render;

export import :display;
