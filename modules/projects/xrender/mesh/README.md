# xrender.mesh

Meshes drawn. What [xrender.scene](../scene) extracted, as pixels: the colour, and beside it the depth, the surface's normal, how far it moved since the last frame, and who drew it — the instance, the cluster and the triangle. This is the **CPU reference** (the foundation plan's §5.1–§5.3, §5.7; D-164, D-184): the same bits on every machine, what a GPU path of the same contract is compared with, and what a test or an agent with no GPU looks at.

| the yard | who drew each pixel (a colour a cluster) |
|---|---|
| ![a floor to the horizon, a red box, a blue ball, a yellow post, a lamp and its light on the floor](media/yard.png) | ![the same frame coloured by instance and cluster](media/yard-ids.png) |

Both are drawn by `tests/mesh_test.cpp` with no window and no device; the first one's bits are pinned there.

```cpp
import xrender.mesh;
namespace mesh = xrender::mesh;

mesh::Pass pass;
mesh::Targets frame;
frame.resize(480, 270);
scene::extract(world, view, seen);
pass.draw(world, seen, view, {.meshes = meshes, .materials = materials}, frame);
const auto who = mesh::at(frame, 240, 135);     // instance 11, its cluster and triangle, the depth and the normal there
```

| | |
|---|---|
| **One pass, five outputs** | colour (rgba8, sRGB), depth, normal (world), velocity (pixels since the last frame drawn), ids (draw, cluster, triangle). The four beside the colour are there from the first frame because each has a consumer now: the depth for what hides what, the ids for an agent's "who drew this pixel", the velocity for anti-aliasing over time, the normal for what lights a surface later (the plan's §9.4) |
| **Exact filling** | A triangle is clipped to the view's volume (and a guard band about it), its corners snapped to 1/256 of a pixel, and filled by the top-left rule with whole-number edges: two triangles that share an edge share no pixel and leave none between them. A sheet of 512 triangles turned 24 ways writes each of its pixels once |
| **The same bits** | IEEE arithmetic in a fixed order, no fused multiply-add, the sRGB curve from a table made with `mlibs.math`'s own powers. Either depth convention (the nearest at 0; or at 1 with no far plane) gives the same picture |
| **Clusters** | A mesh is drawn cluster by cluster (`mlibs.geom`'s `ClusteredMesh`). One outside the view, or whose triangles all face away (its cone), is left out — and no pixel changes (`cull_clusters` off draws them all; the tests hold the two to the same bits) |
| **Velocity** | The pass remembers where each instance was and how it was seen; a pixel says how far its point of the surface moved on the screen. What stood still under a still camera says exactly 0. `forget()` after a cut |
| **Shading** | The least that shows a form: a matt surface under suns, lamps and spots, a constant light from everywhere, and what the surface gives off. No shadows, no roughness or metal, no textures yet: those are the material interface's and the GPU path's (below) |

## Agent surface

Part of the frame's subject ([xrender.frame](../frame)); what this package gives it:

| | |
|---|---|
| Source | `at(targets, x, y)`: the instance (the scene's stable id), its mesh and material, the cluster and the triangle, the depth and the normal at a pixel — or that nothing drew it |
| Observation | `shown(targets, out)`: every instance that reached the frame, with how many pixels and the box of them. An instance that was a draw (`xrender.scene`'s `extract`) and is not shown is behind something; `scene::why_unseen` says why one was no draw at all |
| Numbers | `Pass::last()`: draws, clusters looked at and left out (outside, facing away), triangles (outside, cut, facing away, filled), pixels written and hidden |
| In a frame | `objects(targets, out)` and `describe(targets, x, y, members)` make this picture a layer of [xrender.frame](../frame) (`PictureLayer`): `frame.at` on a pixel of it answers with the instance, mesh, material, cluster, triangle, depth and normal |
| Not here | Control and time (the scene's owner's) |

## Not yet

- **The GPU path**: the same pass through `mlibs.rhi` with Slang materials (base, roughness, metal, textures), compared with this reference within a stated tolerance; shadows and fog; anti-aliasing over time from the velocity.
- **An optimiser's clusters** and levels of detail (meshoptimizer through mwrap): the clusters here are a mesh's triangles in the order they come.
- **Speed**: this is a reference, one thread, a pixel at a time. Its cost is measured (`render/mesh-reference-yard` in `tests/bench`) so that it is known, not so that it is fast.

```sh
mcpp test -p xrender.mesh
```
