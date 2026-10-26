# openmui-raster

A deterministic CPU 2D rasterizer (module `openmui.raster`, D-43): the reference renderer of openmui.

- **Coverage in exact integers.** Curves are flattened in floating point (1/32 px tolerance, no contraction), endpoints rounded to 1/256 px, and each edge adds cover and area to the cells it crosses (the scheme of FreeType's ftgrays); a sweep per row applies the non-zero or even-odd rule. Axis-aligned rectangles have an analytic fast path.
- **Paints**: solid colours, linear / radial / conic gradients (pad, repeat, reflect) through premultiplied lookup tables, bilinear or nearest image patterns.
- **Strokes**: width, butt / round / square caps, miter / round / bevel joins with miter limit, dashes — as positively oriented polygons whose non-zero union has no seams.
- **Compositing**: premultiplied 8-bit source-over with exact /255 rounding; layers with opacity and the 14 W3C blend modes; scissor and mask clips; CSS box shadows (outset and inset) blurred by the W3C three-box approximation.
- **Provenance**: an optional object-id buffer records which node drew each pixel.
- **Bands of rows are exact.** Every crossing of a row boundary is computed from the edge's own endpoints and rows outside the clip are skipped, not cut, so `Canvas::restrict_rows(top, bottom)` draws those rows byte-for-byte as drawing the whole surface does. Damage-region rendering stands on this.
- **No allocation per frame** for a canvas that is reused (`reset`): rasteriser cells, scratch paths and coverage, layer surfaces and clip masks come from pools.

The tests pin the bytes of a scene that GCC, LLVM (debug and release) and the Windows target reproduce.
