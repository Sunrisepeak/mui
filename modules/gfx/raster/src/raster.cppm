// openmui.raster — a deterministic CPU 2D rasterizer (D-43).
//
//   :surface     premultiplied RGBA surfaces, coverage masks, image views
//   :rasterizer  paths to anti-aliased coverage in exact integer arithmetic
//   :paint       solid colours, gradients, image patterns
//   :stroke      stroke outlines: joins, caps, dashes
//   :canvas      compositing, clips, layers with blend modes, shadows,
//                images, the object-id buffer
//
// The same drawing gives the same bytes on every platform: the reference
// renderer of openmui (B-level baseline, spec R-2).
export module openmui.raster;

export import :surface;
export import :rasterizer;
export import :paint;
export import :stroke;
export import :canvas;
