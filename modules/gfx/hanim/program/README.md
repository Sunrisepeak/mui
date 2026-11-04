# hanim.program

Immutable bound expressions and prepared per-object schedules. This package
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
Nested sequence/parallel/stagger preserve explicit scheduling. Strict whole-object
overlap is rejected; complete per-channel blends and effect scopes remain future
work in the [composition design](../../../../.agents/docs/2026-11-04-hanim-play-api-design.md).

The [contract](CONTRACT.md) defines boundaries, gaps, instantaneous stages,
versions, preparation limits and failure atomicity. Read tracks()/segments() for
queryable node paths, versions, schedules, plans and output requirements.
sample(track,time,buffers,effects) writes caller-owned reusable storage; the
returned segment index locates the current plans for an optional renderer.
PreparedProgram::single normalizes already-prepared morph/effects, including a
retarget with an empty from() Snapshot, into this same sampling path.

Preparing may allocate; steady sampling does not. Preparation budgets count all
retained segments while track capacities count maxima needed for mutable output.
Effects retain explicitly authored per-stage endpoints; geometry continuity does
not imply automatic effect/material history matching. This package owns no
clock or mutable playback lifecycle: the engine consumes its prepared data.
