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
## Three dimensions

| Partition | What it holds |
|---|---|
| `:solid` | `Box3` (empty as made, grown by points, `moved` by a matrix), `Sphere` and `sphere_of`, `Plane`, `Ray` and `meet` (a box, a sphere, a triangle; never behind where it starts), `Frustum::of(world_to_clip)` with `holds` — conservative: it never drops what is seen; a reversed perspective with no far plane has five planes |
| `:mesh` | `Mesh` (positions, optional normals, tangents and uvs, indices), `check`, `bounds`, `compute_normals`; the stored form `ClusteredMesh` (version 1): clusters of up to 124 triangles, each with a bounding sphere and the cone its triangles face within (`faces_away`), and the places a hierarchy of clusters fills later; shapes made by code — `box`, `sheet`, `sphere`, `cylinder` — with normals, tangents and uvs, closed bit for bit |
| `:bvh` | `Bvh` (version 1): a tree of boxes over things that have boxes — `bvh_of` (boxes, or a mesh's triangles), `check`, `each_within` a region, `nearest` along a ray (what the caller's `meet_item` says of each thing), a mesh's `meet`. The same things make the same tree bit for bit (cut at the middle thing, by centre then by number), a query walks it in a stated order and allocates nothing; of two things as near, the lower number. The ray query is a capability of geometry (D-184): physics' sweeps, picking, lines of sight, sound's occlusion and a renderer's tracing stand on it. |
| `:walk` | `walk(ray, lattice, visit)`: a ray stepped through a lattice of cells, each cell it passes once and in order, with how far along it was entered and through which face — the same flat (one cell deep) and deep. A lattice is its own acceleration structure: light through a world of cells, a line of sight. From outside it enters where it meets the lattice; through an edge or a corner it steps one axis at a time, the lower first; it allocates nothing. |

Conventions are `mlibs.math`'s: right-handed, +Y up, triangles counter-clockwise seen from their front, depth 0 … 1. `clustered` cuts triangles in the order they are in; an optimiser that puts near triangles together first (meshoptimizer, to be joined under this form) makes better clusters of the same kind.
