# mlibs.geom

The geometry openmui's modules share (module `mlibs.geom`): points, sizes, rectangles, insets, corner radii (with CSS fitting), colours, affine transforms, and the normative pixel snapping (edges, not sizes, so shared edges stay shared at every scale). Standard library only; every function is constexpr.

The canonical package/import is `mlibs.geom` and the API is `mlibs::geom`.
The legacy `openmui.geom` import remains in this same package, sharing
the same implementation and types; C++ consumers must rebuild. The old API is
`omui::geom`. No separate compatibility package is added.
See the [namespace migration guide](../../../.agents/docs/2026-11-05-product-namespace-migration-design.md#6-n1第二组几何内存unicode与搜索).
