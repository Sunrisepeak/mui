# hanim — the independent animation product

The object and playback layers of the independent hanim product. These
language modules do not depend on the UI framework, its specification or games.
The deterministic [hanim.core](../../base/hanim/core) remains below this
layer, using only the standard library and `mlibs.math`.

| Internal module / feature | Responsibility |
|---|---|
| [hanim.effect](src/effect) / `effect` | Owned ordered parameter plans and reusable effect recipes; backend binding stays optional |
| [hanim.engine](src/engine) / `engine` | Explicit-clock playback and batch sampling, controls, current-pose retarget and typed queries |
| [hanim.library](src/library) / `library` | Named-part contour morphs, property recipes and independent channel plans, caller-owned frames and deep C0 retarget preparation |
| [hanim.object](src/object) / `object` | Generational object/part identities and owned immutable captures of hierarchical 2D parts, actual contours and solid fills |
| [hanim.program](src/program) / `program` | Immutable nested bindings compiled into owned object schedules; bounded arbitrary-time sampling without a player |
| [hanim.render.vector](src/render/vector) / `render-vector` | Optional renderer consumer: current vector frames, shared native material mixing and effects |
| [hanim.control](src/control) / `control` | Optional native openas-authorized operations, discovery and typed receipts over the same Engine |

This is the only `hanim` package, exporting core, object, library, effect,
program and engine. `import hanim;` preserves the existing core vocabulary;
`hanim.core` stays at `modules/base/hanim/core` for minimal consumers. Local
path dependencies formerly pointing to `modules/base/hanim` must point here.
There is no UI, specification, renderer, RHI or window dependency in the default
product closure. Rendering is an explicitly selected feature in this same package.

mcpp currently normalizes this existing unnamespaced package to
`mcpplibs.hanim` in resolution metadata; the dependency key and module are
`hanim`. The six old child-package dependency keys and `-p` selectors are
replaced by this package plus features; their C++ module imports are unchanged.
There are no forwarding micro-packages or duplicate implementations.

```toml
[dependencies]
# Full animation product, no renderer (default engine implies program/library/object/effect):
hanim = { path = ".../modules/gfx/hanim" }
# Alternatives to the line above:
# hanim = { path = ".../modules/gfx/hanim", default-features = false, features = ["effect"] }
# hanim = { path = ".../modules/gfx/hanim", features = ["render-vector"] }
# hanim = { path = ".../modules/gfx/hanim", features = ["control"] }
```

`render-vector` implies program/library/object/effect, not the player. A consumer
can turn defaults off and use it without Engine. Feature requests are additive
across dependency edges. The generated `hanim:selected` partition exports only
selected modules; disabled sources and dependencies are not compiled. There is
no runtime feature dispatch or separate authoring API. `control` implies Engine
and the shared independent openas authorization library; it does not imply a
renderer, transport or internal AI. Direct Engine use remains available.

This package is independent because unrelated hosts can create, prepare, sample
and control animations through its owned public contracts without UI or windows.
The effect/object/program layers cooperate inside that product and need no
independent version/release manifests. `hanim.core` remains a separate library:
UI motion, raster and animation share its deterministic std-plus-math functions
without depending on the object registry, player, logging or renderer.

Run `mcpp test -p hanim --locked` for the nine renderer-free test programs.
The [feature consumer](../../../tools/ci/hanim-features/README.md) checks seven
actual source/dependency closures. Renderer tests live in the existing external
[playback consumer](../../../tools/ci/hanim-play-consumer/README.md), which tests
the opt-in feature separately from its production program's dependency audit.

For neutral paths from applications, rendering or glyph outlines,
`hanim::import_path(mlibs::geom::PathView)` captures owned contours with source
command mappings. Its output feeds ordinary HObjects without a renderer
dependency. The object feature includes the std-only shared geometry library;
the core/effect-only choices do not. See the [object contract](src/object/README.md).

For ordinary motion, supply an object and the property target:

```cpp
using namespace std::chrono_literals;
auto playback = hengine.play(hanim::anim::move_to(card, {120, 40}).duration(300ms));
auto trip = hanim::sequence(
    hanim::anim::move_to(card, {120, 40}),
    hanim::anim::move_by(card, {20, -10}),
    hanim::anim::rotate_to(card, std::numbers::pi));
auto prepared = hengine.prepare(trip);
```

`move_to`, `move_by`, `rotate_to`, `rotate_by`, `scale_to`, `scale_by` and
`fill_to` are public property recipes. They accept live ObjectRefs or frozen Snapshots, the same duration,
easing and effects as morph, and the same sequence/parallel/stagger/player.
For several properties on one object, use one typed target:

```cpp
auto change = hanim::anim::to(card, {
    .position = hanim::Vec2{120, 40}, .rotation = std::numbers::pi,
    .scale = hanim::Vec2{1.2, 1.2}})
    .with_effect(hanim::fx::translation_path(hanim::PointPath::bend(.25)));
auto badge = hanim::anim::to(card, {.fill = hanim::Rgba{1, 0, 0, 1}, .part = "badge"});
auto selected = hanim::anim::to(card, hanim::PropertySet{
    {.position = hanim::Vec2{120, 40}, .part = "body"},
    {.rotation = std::numbers::pi, .part = "icon"},
    {.fill = hanim::Rgba{0, 0, 1, 1}, .part = "icon"}});
```

A `PropertySet` applies different properties to several parts in one atomic
animation with shared timing. Unselected local fields retain their source;
parent transforms still propagate. This is not a world-space pin or independent
per-part timing. Two entries writing the same part's translation (including
position versus offset), rotation, scale or fill return `property-conflict`,
including aliases of the root. Retarget accepts the same set and derives every
target from the complete current pose before replacing the plan.

The default selection is the unique object root; named parts use stable ids.
Positions are local to the parent, and rotation/scale use the part's local
origin. These are explicit rotations in radians, including full turns, not
shortest-angle guesses. A sequence preserves unmodified properties from its
actual prior endpoint. `move_by` and `retarget(playback, PropertyTargets{...})`
use that actual pose. No synthetic target object or business-state mutation is
performed. `PreparedObjectMorph::to()` is empty for a computed property target.

`rotate_by(object, radians)` adds to the actual source angle; it does not wrap
or choose the shortest turn. `scale_by(object, {x, y})` multiplies each source
scale by the requested factor, then interpolates between those endpoints with
the existing scale lane. It is not exponential interpolation or an additive
scale offset. These operations preserve pre-existing orientation/size and can
be composed on independent channels:

```cpp
auto feedback = hanim::parallel(
    hanim::anim::rotate_by(card, .12).duration(200ms),
    hanim::anim::scale_by(card, {1.08, 1.08}).duration(200ms));
auto title_feedback = hanim::anim::to(card, {
    .part = "title", .rotation_offset = .12,
    .scale_factor = hanim::Vec2{1.08, 1.08}});
```

Relative fields also work in atomic part sets, direct library timelines and
retargets. Absolute/relative forms of the same channel are mutually exclusive;
parallel writes still conflict, even if one is relative. Negative/zero factors
follow the underlying object's signed/singular scale contract; a UI adapter
may reject a pose that cannot support its input mapping. All values and computed
endpoints must be finite. This extends the property vocabulary, not the count
of independently designed animations toward the 40-design acceptance gate.

This first property domain is the existing closed vector object representation.
Open contours, arbitrary application channels, cameras and rigs are not yet
covered. For different timings on the same object, compose independent channels:

```cpp
auto independent = hanim::composite(hanim::parallel(
    hanim::anim::move_to(card, {120, 40}).duration(600ms),
    hanim::anim::to(card, {.rotation = std::numbers::pi, .part = "icon"})
        .duration(250ms).easing(hanim::ease::out_quad)))
    .with_effect(hanim::fx::blur(0, 2));
auto playing = hengine.play(independent);
```

Translation, rotation, scale and fill are distinct channel groups; each part
has its own channels. Sequence branches can run alongside other channels.
Overlapping writes to one channel still fail; whole-object morph and explicit
snapshot-reset bindings remain exclusive. Shared image effects wrap the
parallel expression; overlapping leaf image effects or different image scopes
are rejected instead of silently widened or duplicated. Additive blending,
world-space constraints and complete UI adapter migration remain future work.

The compiler retains author nodes plus `segments().contributions`, and exposes
prepared channel timing, easing and path metadata. `hengine.plan(playback)`
returns an immutable plan lease for native tools/Agents, including simple play.
The advanced library entry is `PreparedObjectAnimation::property_timeline`;
`PreparedObjectMorph` remains the same compatible type. Its `sample_source`
restores the exact source, distinct from authored jump-start/instant clips at
ordinary time zero. New capability is not counted as completing 40 recipes.

```cpp
import std;
import hanim;
using namespace std::chrono_literals;

// source and target are ObjectRefs owned by hengine; snapshots also work.
auto animation = hanim::anim::morph(source, target)
    .duration(600ms)
    .easing(hanim::ease::in_out_cubic)
    .with_effect(hanim::fx::vertex_path(hanim::PointPath::bend(0.25)))
    .with_effect(hanim::fx::blur(0, 2));
auto played = hengine.play(animation);
if (!played) { report(played.error()); return; }
// ... sample/control, then release the instance before replaying its object.
if (auto released = hengine.release(*played); !released) { report(released.error()); return; }

auto prepared = hengine.prepare(animation);
if (!prepared) { report(prepared.error()); return; }
auto replay = hengine.play(*prepared);
if (!replay) { report(replay.error()); return; }
```

The leaf builder produces the existing immutable `Program`, not a second
authoring tree/compiler/player. `sequence(a,b)`, `parallel(a,b)` and
`stagger(100ms,a,b)` compose morph leaves and advanced Programs. Setters return
new leaf values; they do not capture or mutate live objects. `ObjectRef` inputs
are captured at prepare, while explicit `Snapshot` inputs stay frozen. The
owning thread must not concurrently mutate the registry; Engine preparation
rejects reentrant writes. Prepared geometry remains owned after source removal.

`play(animation, ProgramLimits)` is a cold-path convenience. For a frame-budgeted
caller, prepare explicitly and reuse `PreparedProgram`; `play(prepared)` still
reserves playback buffers. Advanced bindings and the prior
`play(object, anim::morph_to(target), effects, options)` remain available.
Typed `fx::vertex_path(PointPath)` and `fx::translation_path(PointPath)` alter
matched control-point and local-position trajectories before rendering. They
compose with the ordered image-effect parameters. See [the effect contract](src/effect/README.md)
for units, additive path composition and limitations. These are leaf effects,
not group/image scopes. For the latter, use an explicit animation expression:

```cpp
auto animation = hanim::composite(hanim::parallel(
    hanim::anim::morph(card, panel),
    hanim::anim::morph(icon, final_icon)))
    .with_effect(hanim::fx::blur(0, 4));
auto playback = hengine.play(animation);
```

The scope applies once to its combined child output. Its parameters and child
frames are published together by `program_frame`; the optional
`PreparedVectorProgram` consumes them through the shared renderer. Unsupported
topologies, invalid inputs, limits and conflicting writes return typed failures;
no fallback silently discards an effect. Outputs never write business objects.

[The external consumer](../../../tools/ci/hanim-api-probe/README.md) builds and
runs dev/release from only this package, auditing the actual dependency graph.

The public player first supports strict named-part contour/transform/fill
morphs, typed point-path modifiers and ordered effect parameters; the optional vector adapter records the
current geometry through the existing renderer. Pixel/glyph/rig domains, full
channel mixing and catalog, GPU resource scheduling and the complete UI adapter remain
implementation work. CPU/GPU renderer support is evaluated by its own adapter
tests, not inferred from effect-parameter interpolation.

Three reusable feedback designs are public ordinary `Program` factories:

| Recipe | Default | Motion over normalized time |
|---|---|---|
| `anim::pulse(object, factor, options)` | scale factor `{1.08, 1.08}` | source → scaled peak at 1/2 → source |
| `anim::shake(object, amplitude, options)` | parent-space offset `{8, 0}` | source → +offset at 1/4 → −offset at 3/4 → source |
| `anim::wobble(object, radians, options)` | angular amplitude `0.12` radians | source → +angle at 1/4 → −angle at 3/4 → source |

All arguments after `object` are optional. `anim::FeedbackOptions` supplies
`duration` (400 ms by default), per-leg `easing` (`in_out_sine` by default), and
an optional stable `part` id (empty selects the root). Rotation and scale use
that part's local origin; these recipes do not introduce a different pivot or
world-space constraint. No UI, renderer, event loop or script is needed.

```cpp
auto response = hanim::parallel(
    hanim::anim::shake(card, {8, 0}, {.duration=400ms}),
    hanim::anim::pulse(card, {1.12, 1.12}, {.duration=400ms, .part="action"}));
auto animation = hanim::composite(response).with_effect(hanim::fx::tone(
    {.amount=0}, {.amount=.2f}));
auto playback = hengine.play(animation); // Check the normal typed result.
```

The factories lower into the existing property sequences at authoring time.
They have no hidden state or per-frame arithmetic beyond the shared sampler.
The high-level definitions live in `src/program/feedback.cpp` because they
compose Programs; `hanim.library` still owns prepared property/geometry plans
and never depends upwards on the compiler. No new package or runtime is added.

Each return leg is relative to its actual preceding endpoint. Pulse factors
must have finite, nonzero representable reciprocals and finite derived targets;
invalid options/targets fail normal preparation before publication. Signed
factors inherit the base representation's scale behavior and can be unsuitable
for an interactive UI's inverse mapping. Returning to the source is subject
to normal floating-point arithmetic, not a bitwise undo contract. Repeated
sampling of a fixed prepared plan does not accumulate drift. Unselected
channels and business state remain intact; external image effects retain their
own timing/endpoints and do not implicitly undo when a motion returns.

Headless review: prepare with the actual target, inspect the retained Program
nodes/channels, sample the whole duration including 0, 1/4, 1/2, 3/4 and 1,
render intermediate frames with the selected renderer, then reverse the sample
order and verify the same pixels. Check real component actions at displaced
poses; use the host's ordinary motion preferences and cancellation policy for
live playback. The site note's “Feedback combination” and independent UI
consumer use these same factories. These are three feedback designs, not three
new runtime primitives or proof that the full 40/20 catalog is complete.

Invalid feedback parameters produce a queryable `ProgramKind::invalid`
expression before reciprocal/doubled-amplitude arithmetic. The same
`invalid_program(ProgramFailure)` mechanism is available to custom recipe
libraries; preparation reports the actual node in the composition and keeps
the original typed cause. Positive durations too small to retain each leg are
refused rather than silently turning a phase into an instantaneous step.
This does not change valid feedback pixels or require another error transport.
