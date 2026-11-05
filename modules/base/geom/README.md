# mlibs.geom

The geometry the independent modules share (module `mlibs.geom`): points, sizes,
rectangles, insets, corner radii (with CSS fitting), colours, affine transforms,
normative edge-based pixel snapping, and neutral 2D path data. Standard library
only; value operations and path validation/reading are constexpr. Curve
flattening is compiled once in this library.

`PathVerb`, `PathView`, `MutablePathView` and `FillRule` are the common path
encoding. Renderer and raster public names alias these exact types, so their
boundary neither copies command arrays nor depends on matching enum ordinals.
`inspect_path` and `PathReader::make` preflight finite coordinates, operand
counts, contour starts and budgets, reporting the original command/point index.
The reader borrows arrays that must stay alive and unchanged; it allocates
nothing. A draw after `close` starts a new contour at the previous start.
These checks do not prove simplicity, winding compatibility or manifold topology.

`PathPolylines` and `flatten_path` hold the existing rasterizer's curve-to-chord
algorithm. It appends contours in the requested affine space, retains open/
closed flags and preserves the prior numeric operation order. This low-level
operation requires valid input and a finite positive tolerance; use the shared
validator at untrusted boundaries. Its existing 256-chord cap is an approximation
policy, not a guaranteed error bound at arbitrary scales. Certified adaptive
subdivision, generated-vertex provenance and 3D mesh conversion remain future work.

Rendering, rasterization and text outlines consume this contract; Hanim's
`import_path` adapter captures its actual controls without polygonizing them.
Object identity/revision belongs to the source host. Source command indices do
not become persistent IDs by themselves. Geometry contains no player, renderer,
UI, device, logging or Agent transport dependency. Library/API consumers can
inspect the same typed data without a window.

The canonical package/import is `mlibs.geom` and the API is `mlibs::geom`.
The legacy `openmui.geom` import remains in this same package, sharing
the same implementation and types; C++ consumers must rebuild. The old API is
`omui::geom`. No separate compatibility package is added.
See the [namespace migration guide](../../../.agents/docs/2026-11-05-product-namespace-migration-design.md#6-n1第二组几何内存unicode与搜索).
