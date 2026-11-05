# hanim.effect

Owned, ordered effect parameter plans (`import hanim.effect;`). This internal module
depends on `hanim.core`, independent logging and tracing; it has no UI,
renderer, shader, device, window or playback loop dependency.

Typed process effects are separate from the image-parameter chain:

```cpp
auto animation = hanim::anim::morph(source, target)
    .with_effect(hanim::fx::vertex_path(hanim::PointPath::bend(0.25)))
    .with_effect(hanim::fx::translation_path(hanim::PointPath::bend(-0.2)))
    .with_effect(hanim::fx::blur(0, 8)); // import hanim for authoring and Engine
```

`VertexPath` modifies each matched vertex and Bezier control's interpolation,
after correspondence. `TranslationPath` modifies each part's local tx/ty;
parent transforms still compose normally. It does not select only the root or
alter geometry, paint, rotation or scale. Typed targets prevent accidentally
sending a trajectory modifier to an image backend. They lower to the existing
morph preparation, not a second player or renderer. Changing matching rules,
and arbitrary property-channel bindings remain separate work. Image effects
on composed programs use explicit composite scopes (see `hanim.program`);
they are not copied to every leaf.

`PointPath` stores two cubic control offsets in the endpoint chord frame:
x along `(to-from)`, y along `(-dy,dx)`. `bend(f)` gives midpoint displacement
of `f * chord length`; positive y in screen coordinates points down. Custom
control pairs express asymmetric paths. Coordinates are local to the animated
data, not implicitly world space. Endpoints and stationary points stay exact.
These are parametric paths; easing supplies progress, not constant arc speed.

Repeated path effects on one target **add displacement in the same endpoint
frame**. They do not compose sequential deformations; image effects retain
their ordered chain. The accumulated controls must be finite and within
±1e6, checked at preparation. Core sampling is stateless and allocation-free;
object frames validate representability before writing. C0 retarget captures
the actual modified geometry/position; no velocity/C1 guarantee is added.
The operator neither solves topology nor guarantees simple intermediates.

The original image-parameter API remains available:

```cpp
auto descriptions = hanim::fx::chain({
    hanim::fx::blur(0, 8),
    hanim::fx::tone({1, 1, 1, 0}, {0.2f, 0.4f, 1, 0.8f})
});
auto plan = hanim::PreparedEffects::prepare(descriptions);
if (plan) {
    std::vector<float> output(plan->parameter_count()); // allocate once
    auto result = plan->sample(0.5, output);
}
```

The chain is applied first-to-last by a backend. Repeated effect ids retain
their positions. Each effect's named parameters retain their own order and
occupy its `EffectEntry` offset/count slice of sampled output. `blur` names a
logical-pixel `radius`; `tone` names tint `color_r`, `color_g`, `color_b` and
`amount`, matching the existing vector renderer's recipes. These helpers
describe parameters; they do not implement or execute a blur/tint algorithm.

Preparation deep-copies every id, parameter name and endpoint. Copies of a
prepared plan share immutable owned data. Borrowed spans require a retained
plan and cannot be obtained from a temporary. Concurrent readers may sample
one plan into separate caller-owned buffers. Output never refers to input
descriptions. No clock, hidden scratch or per-sample allocation is introduced.
Sampling delegates interpolation to core, clamps finite progress to [0,1]
and preserves exact endpoints. Errors leave the entire output untouched;
unused output suffixes are also untouched.

Keys are nonempty, at most 128 bytes, with no ASCII controls. Effect ids may
repeat; parameter names must be unique within each effect. Finite endpoints
and representation limits are checked. Limits apply per plan: defaults are
64 effects/1,024 parameters, hard maxima 4,096/65,536. Empty plans are valid;
zero-parameter effects can represent actual backend effects with no controls.
Allocation exhaustion follows the process allocator policy.

Successful preparation is **not** a renderer capability assertion. Unknown
effect ids and finite out-of-backend-range values remain descriptive data.
Binding must reject unknown effects/parameters, unsupported CPU/GPU paths,
invalid endpoint ranges and stale resource generations. It must never silently
omit an unsupported effect. Renderer binding/application is a separate adapter.

Preparation success/failure logs use `hanim.effect`; prepare/sample trace spans
use the same category. Sampling does not format logs. Trace's existing thread
buffer may allocate when tracing first attaches, before warm sampling.

`fx::pixelate` describes the shared renderer's grid and strength:

```cpp
auto blocks = hanim::fx::pixelate(
    {.size = 6, .amount = 0}, {.size = 12, .amount = 1},
    {.unit = hanim::fx::PixelUnit::device, .origin_x = -2, .origin_y = 3});
auto animation = hanim::anim::morph(source, target).with_effect(blocks);
auto playback = hengine.play(animation); // import hanim; Engine hengine
```

The grid is layer-anchored and aligned to output axes. Logical units scale
with output DPI; device units stay in device pixels. Offsets use that same
unit. Exactly zero `amount` bypasses filtering while retaining ordinary layer
opacity, blending and nested effects. One logical pixel is not an identity
at high DPI. Strength is quantized to 1/255 by the shared CPU/GPU renderer.
This recipe is neither a palette/dither system nor a complete UI style pack.

Minimal consumer SOP: prepare public object snapshots, compose animation and
effect recipes, `play`/`seek` and inspect the frame, optionally bind the vector
adapter and capture actual offscreen pixels, compare off/mid/end and replay,
then release playback. Check every `expected` result and adapter capability;
preparation success alone does not guarantee rendering support. The
[independent public API probe](../../../../../tools/ci/hanim-api-probe) runs the
parameter path without UI or a renderer; `hanim.render.vector` tests exercise
the optional real-pixel path. No GUI is required in either workflow.

Tests: `mcpp test effect/ -p hanim --locked` after workspace registration.
