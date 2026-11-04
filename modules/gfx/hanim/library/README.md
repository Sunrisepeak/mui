# hanim.library

Public object animation plans, independent of UI, specification and renderer.
This first recipe matches immutable `hanim.object` snapshots by explicit part
id, preserves source sibling ordering, and prepares every contour through the
same `hanim.core::PreparedShapeMorph` implementation. Decomposed transforms
and solid sRGB fills use the core `lerp` and its premultiplied OKLab lanes.

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
The single recipe here does not constitute 40 designs, a complete animation
catalog, an Engine, a clip graph, pixel transport or arbitrary topology support.
See [CONTRACT.md](CONTRACT.md) for the strict capability and lifetime contract.
