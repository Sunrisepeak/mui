# hanim.library

Public object animation plans, independent of UI, specification and renderer.
This first recipe matches immutable `hanim.object` snapshots by explicit part
id, preserves source sibling ordering, and prepares every contour through the
same `hanim.core::PreparedShapeMorph` implementation. Decomposed transforms
and solid sRGB fills use the core `lerp` and its premultiplied OKLab lanes.

`MorphOptions::shape.point_path` applies the public `PointPath` operator to
matched geometry. `translation_path` applies it to each part's local tx/ty.
Both default to the original linear interpolation. These process effects
retain correspondence and exact endpoints; invalid controls fail preparation,
and an overflowing translated frame fails before writing any output. The
authoring layer exposes these options as typed effects without a renderer
dependency. Other transform lanes and colors keep their existing interpolation.

```cpp
import hanim.library;
// source and target are owning Snapshot values.
auto plan = hanim::PreparedObjectMorph::prepare(source, target);
if (plan) {
    const auto count = plan->requirements();
    std::vector<hanim::FramePart> parts(count.parts);
    std::vector<hanim::ShapeCubic> cubics(count.cubics);
    auto frame = plan->sample(0.5, {parts, cubics});
    // parts[i].info: id, endpoint refs, parent and contour range.
    // frame->contours: flat cubic ranges into frame->cubics.
}
```

`anim::morph_to(target_ref, options)` is a descriptor for a caller's Engine;
it starts no clock, modifies no registry and resolves no target by itself.
The library also prepares property targets and independent property timelines.
The canonical prepared type is `PreparedObjectAnimation` (the same type as the
compatible `PreparedObjectMorph`). Timelines retain one source geometry and
indexed translation/rotation/scale/fill channel segments; arbitrary-time sampling
does not replay history. These recipes do not constitute 40 designs, a complete
animation catalog, pixel transport or arbitrary topology support.
See [CONTRACT.md](CONTRACT.md) for the strict capability and lifetime contract.


The public CTS in `../../tests/library/morph_test.cpp` checks strict contracts, geometry
endpoints, C0 retarget with destruction of the old payload/snapshot tables,
caller-buffer failure atomicity, warm sampling with active shared trace, and
native concurrent reads. Run `mcpp test library/ -p hanim` in the containing workspace. Web runs the
same non-threaded contracts; native
thread support additionally enables the concurrent-buffer case.
