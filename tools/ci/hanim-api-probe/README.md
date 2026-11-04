# Hanim authoring API experiment

This external consumer tests an API candidate against the existing public
Hanim implementation. It does **not** add or freeze a product API. Its only
library dependency is `hanim.engine`; `api_probe` belongs to this test project.

The experiment uses an animation-first factory with ordinary arguments. These
are the exact supported calls (`source`/`target` are Engine-owned `ObjectRef`s,
`engine` is an existing `hanim::Engine`):

```cpp
import std;
import hanim.validation.api_probe; // consumer fixture, not a product module
using namespace std::chrono_literals;
namespace anim = api_probe::anim;

api_probe::HEngine hengine{engine}; // borrows; engine must outlive it
auto animation = anim::morph(source, target)
    .duration(600ms)
    .easing(hanim::ease::linear)
    .with_effect(hanim::fx::blur(0, 8));

auto playback = hengine.play(animation); // expected<PlaybackRef, EngineFailure>
```

For repeated playback or explicit resource limits, use the same description:

```cpp
hanim::ProgramLimits limits;
limits.max_nodes = 128;
auto prepared = hengine.prepare(animation, limits);
if (!prepared) return std::unexpected(prepared.error());
auto playback = hengine.play(*prepared);
```

These examples are alternative entry paths: starting both concurrently for the
same object gives the existing `binding_conflict` error. `Engine` still owns
seek/pause/resume/release and output buffers. Application code must check errors
and release playback handles; this adapter introduces no automatic clock.

```cpp
auto animation = api_probe::parallel(
    api_probe::sequence(
        anim::morph(card, panel).duration(600ms),
        anim::morph(card, expanded).duration(400ms)),
    api_probe::stagger(100ms,
        anim::morph(title, final_title),
        anim::morph(icon, final_icon)));
```

Timing in a sequence and the second segment's source pose come from the real
`hanim.program` compiler. A later segment for `card` starts from the previous
segment's endpoint, even though its description names the same live `card`.
Parallel writes to the same logical object are rejected; no implicit blend is
invented. Multi-track frames require an explicit track index.

## What the experiment establishes

| Concern | Actual behavior |
| --- | --- |
| Simple and prepared paths | Same `PreparedProgram::prepare` and `Engine::play`; no second matcher, compiler, player, scheduler or evaluator |
| Authoring | Small immutable descriptor tree, normalized to existing `Program`; concepts restrict composition to this fixture's expressions |
| Live inputs | Description retains `ObjectRef`; `prepare` captures the Engine's current revision, not the revision at description creation |
| Frozen inputs | `morph(Snapshot, Snapshot)` uses the explicit owned endpoints; external registries currently require this overload |
| Ownership | `PreparedProgram` retains geometry after original objects are destroyed; stale/foreign refs are rejected before playback |
| Value reuse | `.duration`, `.easing`, `.with_effect` return a new leaf; reusing the base expression preserves it |
| Composition | `sequence`, `parallel`, `stagger` preserve structure until the existing compiler prepares it |
| Effects | Ordered **leaf parameter chains**, tested with blur then tone; nested/group effect scope is deliberately unavailable |
| Errors | `std::expected` preserves nested object/program/effect failures; `[[nodiscard]]` and chrono durations keep mistakes visible |
| Limits | Adapter bounds lowering by node/depth limits; the real compiler checks its remaining budgets and semantic errors |

Creating descriptions, capturing refs and compiling plans are cold-path work.
They may allocate and retain leases; convenient `play(description)` pays that
preparation cost on every call. No implicit preparation cache is claimed. The
facade and Engine require one owning thread or external synchronization. A
prepared plan does not update or write back into the original business object.
Borrowed `EngineFrame` spans still expire on the next engine mutation; the tests
copy frames before seeking again. Engine-owner lifetime is a caller contract,
not a dynamically checked weak reference.

There is no `move`, `reveal`, arbitrary property animation, matching plugin,
disturbance/field simulation, group effect scope, general HObject adapter,
GPU resource graph, renderer, pixel output or OpenAS provider in this fixture.
The effect test checks parameter ordering/interpolation, not the visible blur
or tone result. True contour interpolation is checked through actual cubic
coordinates and object transforms, not a time/progress-only surrogate.
Use [the existing renderer consumer](../hanim-play-consumer/README.md) for its
separate CPU pixel evidence. This test does not establish throughput, allocation
budgets, sanitizer results, cross-platform support or superiority to another
animation engine. The descriptor tree and facade remain a disposable prototype.

## Reproduce and inspect

```sh
MCPP_JOBS=2 python3 tools/ci/hanim-api-probe/run.py \
  --mcpp /path/to/mcpp \
  --output /tmp/hanim-api-probe.report.omui.json --keep-project
```

`--source-root /path/to/checkout` can select the public libraries independently
of the fixture checkout. The script creates a fresh external workspace,
bootstraps its resolution, and builds/runs dev and release with `--locked`.
Only local path dependencies are present; mcpp may create no lockfile for this
closure. The report records that explicitly and checks that both the optional
consumer lock and the source checkout's lock remain unchanged. The root
manifest and workspace membership are never edited.

JSON includes per-check results, commands, exit codes, logs, compiler metadata,
fixture and compiled source hashes, resolution graphs and compilation commands.
The audit requires C++23 compilation of both consumer sources and the actual
Hanim packages; it rejects UI, spec, testing, host, site and rendering packages.
`--keep-project` retains the consumer for inspection. Evidence artifacts remain
beside the report even if the temporary project is removed.

The checks cover real geometry midpoints, out-of-order seeking, simple/prepared
equivalence, source immutability, reusable leaves, effect order, nested tracks,
stagger pre-start poses, sequence endpoint continuity, limits, bad effects,
conflicts, foreign/stale refs, generation reuse and owned prepared geometry.
Several checks guard setup/lifecycle; their count is not a feature count.

[Recorded local evidence](2026-11-05.report.omui.json) retains the actual
dev/release results, toolchain, complete assertion names, ten-library dependency
closure, fixture/source hashes and unchanged source-lock hashes. It is a result
from the recorded source commit, not a guarantee that later revisions pass.
