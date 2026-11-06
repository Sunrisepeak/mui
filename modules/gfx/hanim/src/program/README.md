# hanim.program

Immutable bound expressions and prepared per-object schedules. This internal module
consumes the existing object morph/effect plans and core time mapping, with no
UI, renderer, window, script, transport or player dependency.

Animation-first leaves use `anim::morph(source,target).duration(600ms)` and
`.easing(...)`, `.with_effect(...)`, `.source(ProgramSource::snapshot)`.
Inputs can independently be live `ObjectRef`s or immutable `Snapshot`s. Leaves
convert to this same `Program`; variadic sequence/parallel/stagger also accept
existing low-level Programs. There is no second authoring AST or compiler.
For live inputs, call `PreparedProgram::prepare(program, registry, limits)` or
`Engine::prepare(program, limits)`. Without a registry, live inputs report
`unresolved_object`; snapshot-only descriptions still prepare independently.
`ProgramFailure` retains the offending node and nested `ObjectError`.

```cpp
import hanim.program;
using namespace hanim;
// a, b, c are valid owned Snapshots; a supplies the logical track identity.
auto program = sequence({bind(a, b), bind(a, c)});
auto prepared = PreparedProgram::prepare(program);
```

The second stage begins at the first stage's actual boundary pose. Every stage
is prepared once, so sampling 1.5, 0.25, 2, 0 in arbitrary order gives the same
results as forward playback. B remains a separate object if explicitly bound as
`sequence({bind(a,b),bind(b,c)})`; target identity never implicitly merges tracks.
Nested sequence/parallel/stagger preserve explicit scheduling. Independent
translation/rotation/scale/fill channels on the same object may overlap, with
their own timings; whole-object morph/reset overlap and conflicting channel
writes are rejected. Complete per-channel blends and device resource scheduling
remain work in the [composition design](../../../../../.agents/docs/2026-11-04-hanim-play-api-design.md).

`composite(parallel(anim::morph(a,b), anim::morph(c,d))).with_effect(fx::blur(0,4))`
processes the composed subtree once. Nested scopes preserve their own ordered
effect chains, after leaf effects. `.effect_easing(...)` controls scope parameters
only, not child time; vertex/translation path effects stay typed leaf operations.
The explicit form is `composite(program, Effects, CompositeOptions)`.
`nodes()` retains author preorder, parents, subtree boundaries and absolute time;
`scopes()` exposes parent/depth, time and parameter ranges. Segments identify
their current scope. `sample_scopes(time, output)` uses reusable caller storage,
and `scope_parameter_count()` reports its size. Plain groups introduce no scopes.

The [contract](CONTRACT.md) defines boundaries, gaps, instantaneous stages,
versions, preparation limits and failure atomicity. Read tracks()/segments() for
queryable node paths, versions, schedules, plans and output requirements.

Overlapping property leaves compile to one geometry output, while `nodes()`
retains the original author tree and each participating node's `contribution`
indexes the compiled segment's `contributions`. `animation.channels()` exposes
part, channel, original clip, time, easing, translation path and source policy.
Compiled segment count and authored binding count are different quantities.
Shared image scopes remain explicit; an overlapping group with leaf image
effects or differing image-scope membership reports `scope-conflict`.
sample(track,time,buffers,effects) writes caller-owned reusable storage; the
returned segment index locates the current plans for an optional renderer.
PreparedProgram::single normalizes already-prepared morph/effects, including a
retarget with an empty from() Snapshot, into this same sampling path.

Preparing may allocate; steady sampling does not. Preparation budgets count all
retained segments while track capacities count maxima needed for mutable output.
Effects retain explicitly authored per-stage endpoints; geometry continuity does
not imply automatic effect/material history matching. This internal module owns no
clock or mutable playback lifecycle: the engine consumes its prepared data.

`anim::pulse`, `anim::shake` and `anim::wobble` are reusable public Program
factories in `feedback.cpp`. They lower to the ordinary relative property
sequences, so the same preparation limits, part/channel conflict detection,
source chaining, scoped image effects, arbitrary-time samples and Engine apply.
`FeedbackOptions` shares duration, per-leg ease and named-part selection.
They add no scope when unadorned and no special sampling branch. See the
[product guide](../../README.md) for parameters, phase landmarks and headless SOP.

Custom recipe authors can preserve a typed construction failure without throwing
or fabricating NaN-valued animation data:

```cpp
return hanim::invalid_program({.code=hanim::ProgramError::invalid_time});
```

The resulting immutable value has `ProgramKind::invalid`. Its borrowed
`failure()` pointer is available before preparation and lives as long as that
Program's shared node. Normal and empty expressions return null. As before,
`operator bool` means a nonempty expression, not a successfully validated plan.
Error payload storage is allocated only on the rejected node; copying or
composing it shares the immutable value. `prepare` / `Engine::play` refuses it
with the same typed cause and the actual author-tree node index. The original
payload is not mutated. Node/depth budgets still apply before visiting errors;
no invalid node enters a prepared plan or the sampling path.

Feedback factories check finite/nonzero reciprocal domains, doubled-amplitude
bounds, easing and representable nonzero phase durations before calculating
legs. Zero total duration retains the existing instantaneous-sequence semantics.
Object-dependent target overflow is still validated during ordinary preparation.
The library does not alter the caller's floating-point environment or claim
that all engine operations support unmasked hardware exceptions.

`anim::move_through` in `waypoints.cpp` lowers timed, source-relative translation
waypoints to the same property sequence. Incoming legs reuse `PointPath`;
there is no new sampler, channel or geometric path representation. Source
arrays can be released after authoring. Time/order, ease, path and subtraction
ranges are checked before each leaf is built; invalid inputs return the existing
immutable error expression. See the [product guide](../../README.md#timed-translation-paths)
for units, part selection, composition, continuity and headless review.


`wait(seconds)` is the timeline's object-free interval. `delay(seconds, child)`
composes it with a child using the existing sequence. The compiler stores its
hierarchy/timing and no object segment, preserving ordinary gap fills and
independent channels; the existing Engine handles pure timers too. See the
[product guide](../../README.md#timeline-waits-and-delays) and contract 6.


`repeat(count, child)` retains one immutable child and expands finite serial
iterations in the same bounded compiler. Prepared metadata exposes the count
and per-child iteration, including nested scopes; zero count is rejected.
Relative source chaining and explicit snapshot resets keep sequence semantics.
See [finite repetition](../../README.md#finite-child-repetition) and contract 7.


Nested serial scheduling carries the predecessor's absolute end directly.
Property groups rebase both endpoints and check original-coordinate conflicts;
no overlap tolerance is introduced. A positive interval lost to time-origin
conversion is refused explicitly. The compiler's existing trace/diagnostics
cover this cold work; ordinary prepared sampling remains unchanged.
