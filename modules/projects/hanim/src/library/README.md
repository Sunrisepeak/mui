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
auto plan = hanim::PreparedObjectAnimation::prepare(source, target);
if (plan) {
    const auto count = plan->requirements();
    std::vector<hanim::FramePart> parts(count.parts);
    std::vector<hanim::ShapeCubic> cubics(count.cubics);
    auto frame = plan->sample(0.5, {parts, cubics});
    // parts[i].info: id, endpoint refs, parent and contour range.
    // frame->contours: flat cubic ranges into frame->cubics.
}
```

The library also prepares property targets and independent property timelines.
The prepared type is `PreparedObjectAnimation`. Timelines retain one source geometry and
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


`PropertyTargets::route` and `anim::follow_path` use the core MotionPath's shared
distance evaluator in the ordinary translation channel. Immutable ownership,
part selection, source chaining, image scope composition and route budgets are
part of the [route contract](CONTRACT.md#immutable-translation-routes), not a UI
special case. The product guide distinguishes this approximate distance route
from explicitly timed `move_through` waypoints.
