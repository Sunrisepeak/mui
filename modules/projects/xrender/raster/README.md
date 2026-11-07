# xrender.raster

A deterministic CPU 2D rasterizer (module `xrender.raster`, D-43): the shared CPU reference used by omui and hanim.

Path data, fill rules and polyline storage are aliases of the shared
`mlibs.geom` contract. Curve flattening now lives there; `raster::flatten`
preserves the existing entry and numeric output. Raster owns coverage,
stroking and compositing, without a second path encoding or geometry converter.

- **Sparse cells, runs of coverage.** Only the cells an edge touches are kept (sorted by row and column); between two cells the coverage is the winding's alone, so the sweep yields runs, and a fill composites a run of one value as one colour — a store for an opaque one. Axis-aligned solid rectangles skip coverage altogether. The cost follows the outline, not the area, and the bytes are the dense sweep's.
- **Coverage in exact integers.** Curves are flattened in floating point (1/32 px tolerance, no contraction), endpoints rounded to 1/256 px, and each edge adds cover and area to the cells it crosses (the scheme of FreeType's ftgrays); a sweep per row applies the non-zero or even-odd rule. Axis-aligned rectangles have an analytic fast path.
- **Paints**: solid colours, linear / radial / conic gradients (pad, repeat, reflect) through premultiplied lookup tables, bilinear or nearest image patterns; an image as a mask (`Paint::Kind::image_mask`: its alpha as coverage of the paint's colour). An image paint can be **lit** by a `Material` (`Paint::material`): a normal map sampled where the colour is, turned by the orthogonal part of the paint's transform (`orthogonal_part`: rotation, and mirror), shaded by a light in device space — lambert cut into soft-edged cel bands toward a shadow colour, a rim where the normal grazes toward the light — in IEEE single precision without contraction (`Shader::lit`, `material_setup`; the GPU renderer shades with the same constants). Without a material an image fill takes the same path as before.
- **Strokes**: width, butt / round / square caps, miter / round / bevel joins with miter limit, dashes, trims (the drawn part of each contour by fractions of its length: start, end and an offset that wraps a closed contour; before dashing, `trim_test`) — as positively oriented polygons whose non-zero union has no seams.
- **Material mixtures**: caller-owned `MixShader` reuses the existing leaf shaders and combines their premultiplied colours before the canvas applies shape coverage once. Reused gradient setup builds its lookup table without a temporary stop vector; unordered stops and duplicate hard stops preserve the original shading semantics.
- **Compositing**: premultiplied 8-bit source-over with exact /255 rounding; layers with opacity and the 14 W3C blend modes; scissor and mask clips; CSS box shadows (outset and inset) blurred by the W3C three-box approximation.
- **Provenance**: an optional object-id buffer records which node drew each pixel.
- **Bands of rows are exact.** Every crossing of a row boundary is computed from the edge's own endpoints and rows outside the clip are skipped, not cut, so `Canvas::restrict_rows(top, bottom)` draws those rows byte-for-byte as drawing the whole surface does. Damage-region rendering stands on this.
- **Reused storage**: rasteriser cells, scratch paths and coverage, layer surfaces and clip masks retain their capacity across `reset`. Stroke outline preparation and other renderer work can still allocate; zero-allocation claims apply only to separately measured workloads.

The tests pin the bytes of a scene that GCC, LLVM (debug and release) and the Windows target reproduce.

Dash lists use on/off lengths in path units. Odd lists repeat twice per
cycle, following the [SVG dash-list convention](https://www.w3.org/TR/SVG2/painting.html#StrokeDashing),
and the pattern restarts on each subpath. Negative/nonfinite lengths disable
dashing as a whole in this low-level API; empty/all-zero lists also retain
the solid stroke. A nonfinite offset uses phase zero. Finite offsets use the
shared exact remainder, including extreme magnitudes. `dash_test` compares
actual coverage with an explicitly doubled list and checks invalid inputs
that previously trapped a frame. This does not claim dashed morph phase
continuity under contour start/winding alignment, complete zero-length cap
semantics, or a bounded tessellation budget for arbitrarily tiny dash lengths.