# hanim.effect

Owned, ordered effect parameter plans (`import hanim.effect;`). This package
depends on `hanim.core`, independent logging and tracing; it has no UI,
renderer, shader, device, window or playback loop dependency.

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

Tests: `mcpp test -p hanim.effect --locked` after workspace registration.
