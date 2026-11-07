# Public hanim authoring consumer

This directory began as an API experiment. It now validates the real product:
its only library dependency is `hanim`, and its source imports `hanim` directly.
The consumer-local facade and second description tree have been removed.

```cpp
import std;
import hanim;
using namespace std::chrono_literals;

// hengine is a hanim::Engine; source/target are its ObjectRefs.
auto animation = hanim::anim::morph(source, target)
    .duration(600ms)
    .easing(hanim::ease::linear)
    .with_effect(hanim::fx::blur(0, 8));
auto played = hengine.play(animation);
if (!played) { report(played.error()); return; }
```

Explicit preparation is the alternative entry:

```cpp
hanim::ProgramLimits limits;
limits.max_nodes = 128;
auto prepared = hengine.prepare(animation, limits);
if (!prepared) { report(prepared.error()); return; }
auto played = hengine.play(*prepared);
if (!played) { report(played.error()); return; }
```

These snippets are alternatives, not concurrent plays on the same source.
Release a playback before creating a new binding for its object. Preparation
and playback-buffer reservation are cold work; the engine has no automatic
clock or business setter. Frame spans expire at the next engine mutation.

```cpp
auto animation = hanim::parallel(
    hanim::sequence(
        hanim::anim::morph(card, panel).duration(600ms),
        hanim::anim::morph(card, expanded).duration(400ms)),
    hanim::stagger(100ms,
        hanim::anim::morph(title, final_title),
        hanim::anim::morph(icon, final_icon)));
```

The leaf converts into the existing immutable Program. Its setters return new
values, and group effects are deliberately not a leaf setter on a group. The
same Program compiler handles legacy snapshot bindings and modern leaves.
Live ObjectRefs capture at prepare; explicit snapshots stay frozen, including
mixed live-source/frozen-target inputs. Standalone preparation accepts a registry
for live inputs; without one it reports `unresolved_object`. Foreign/stale refs,
invalid geometry/effects, depth/node budgets and conflicting writes are failures,
not silent fallbacks. Engine failures preserve nested diagnostics.

A sequence's later source starts at the previous actual endpoint unless
`.source(ProgramSource::snapshot)` explicitly requests a reset. Prepared plans
retain owned geometry after source destruction; original business objects never
change during playback. Engine preparation requires the owning thread and
rejects reentrant mutation. `composite(animation).with_effect(...)` retains
explicit nested image scopes. The public probe samples their parameters and
queries atomic program frames without introducing a renderer dependency.
Full channel mixing, device/resource scheduling, image/pixel/rig domains and
complete UI adapters are separate implementation work.

## Run and inspect

```sh
MCPP_JOBS=2 python3 tools/ci/hanim-api-probe/run.py \
  --mcpp /path/to/mcpp \
  --output /tmp/hanim-public-authoring.report.omui.json --keep-project
```

The runner creates a fresh external mcpp workspace, resolves the actual public
umbrella, then runs dev and release with locked resolution. It audits C++23
compilation and the package graph, rejecting UI/spec/renderer/RHI dependencies.
The root source lock and the optional consumer lock must stay byte-identical.
mcpp currently qualifies the pre-existing unnamespaced package as
`mcpplibs.hanim`; the dependency key and module remain `hanim`. The audit also
checks the resolved umbrella source path.

Checks cover real cubic geometry, transforms, effect parameter order, seek,
value reuse, live capture/frozen snapshots, nesting, explicit source reset,
legacy composition, stale/foreign/generation errors and resource lifetime.
Typed vertex/translation paths also exercise direct/prepared equivalence,
changed middle geometry, exact endpoints and simultaneous image effects.
Property recipes and multi-part targets share that compiler. Independent
part/property channels are checked with different timings, retained author
contributions, complete-pose retarget and restore. `hengine.plan(playback)`
retains the immutable plan after retarget/release and refuses stale handles.
These are assertions, not animation-library entries. The separate
[rendering consumer](../hanim-play-consumer/README.md) exercises this public
entry with the shared CPU renderer and real cropped pixels.

The JSON report records commands, checks, toolchain, dependency graph, actual
compiled source hashes and lock hashes. It does not establish GPU performance,
all-platform support, all effect domains or the complete public catalog.
The CI workflow runs this gate; local passing results need not wait for CI.

[The original 76-assertion report](2026-11-05.report.omui.json) belongs to its
recorded historical commit and consumer-local prototype. It is retained as
history and is not evidence for the current public API. Current execution
results are recorded separately in the [S1 public API report](2026-11-05-public.report.omui.json)
with their own source hashes: dev/release each pass 84 assertions, related suites
each pass 40 cases, and the Wasm umbrella executes 3 cases. The same report
includes the independent CPU renderer consumer's results and explicit limits.

[The S2 point-path checkpoint](2026-11-05-path.report.omui.json) records the
subsequent typed process effects: dev/release each pass 94 external assertions
and 178 related native cases, 68 core/umbrella cases execute under the Wasm
fixture profile, and each profile exports 9 CPU pixel cases. General channels,
group scopes and interactive website integration remain outside this checkpoint.

[The subsequent S2 scope checkpoint](2026-11-05-scopes.report.omui.json) records
explicit nested image scopes, atomic program frames and the optional shared
vector renderer: native dev/release each pass 63 cases and 100 independent
consumer assertions; Wasm executes 62 cases (one native threading case excluded).
CPU group/leaf blur differs as expected while the explicit shared-layer oracle
matches. Warm record and scope-seek allocation checks pass. General channels,
scope-preserving retarget, GPU scheduling and the public UI scope adapter remain
open; PPet mixed-language hot replacement is a documented target, not delivered runtime.

The probe also plays `anim::move_through` through the public umbrella, samples a
curved leg, verifies its source-relative offset and image-scope parameters, and
releases the playback. Use `--project /tmp/hanim-api-probe-... --keep-project`
to reuse the latest disposable consumer's incremental build. Existing fixture
locks are used with `--locked`; only a new fixture receives its own bootstrap
lock. The source workspace lock remains unchanged.
