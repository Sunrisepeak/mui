# hanim

A composable, agent-native animation engine: one set of capabilities for UI animation, video and game animation.

The one owner of general animation in the repository (AGENTS.md §2.3): values, curves, channels, representations, orchestration, playback and recipes. It depends on no UI framework, specification or game.

| Package | Layer | |
|---|---|---|
| [hanim.core](core) | base | Deterministic time, values, curves, springs, paths, prepared contour morphs, timelines; the standard library and `mlibs` (math, utils, memory) only |
| `hanim` (this directory) | gfx | Objects, typed channels, recipes, effects, nested and time-mapped programs, the explicit-clock Engine and its interactive mode (`drive`), by feature; rendering is opt-in (`render-vector`) |
| [hanim.rhi](rhi) | gfx | Prepared shape morphs sampled in batches on an mlibs.rhi device (plan §6.8 ⑧): one Slang compute kernel, the CPU sampler's semantics in 32-bit floats, path noise refused (WebGPU has no 64-bit integers); its own package so hanim never imports a device interface; its CTS (`hanim.rhi.cts`) runs in the SDL and WebGPU backends' tests |
| [hanim.visual](visual) | gfx | The visual representation (L3): matter taken from display lists, two drawings matched (by node, by meaning, glyph by glyph) and drawn between as a pure function of time; its own package because it reaches xrender's display lists and carries its own tests (D-120) |
| [hanim.studio](studio) | gfx | The animation tool: `<name>.animation.omui.json` documents (shapes and steps) an Agent writes, checked with every problem where it is, operated through hanim.control and rendered headless by the `hanim` command (`check`, `sample`, `render`); its own package because its dependencies (JSON, image encoding, the CPU raster) are a tool's |

**Representations next (M3.5, convergence plan §6.8):** glyph and path objects, strokes (trim and writing), topology-aligning morphs (A→B), selectors and deformers (displacement that overshoots with a spring, follows a drag), match modifiers and path noise, blending layers on one channel, constraints, elastic bodies (any object falls, squashes on contact and rebounds: a cage deformer, xengine.simulation's soft-body solver shared with physics, motion-derived squash and stretch, an analytic bounce curve), then GPU evaluation. Rig, particles and fields follow in M6.

**Signature features — candidates** (plan §9.5; none is claimed outside the repository before its evidence exists):

| Feature | Kind | Evidence needed |
|---|---|---|
| Bit-identical across platforms: any time can be sought and gives the same result | leading | GCC/LLVM/Wasm hashes equal; determinism against Lottie and Rive |
| One engine for UI, video and games: seekable orchestration and interruptible interactive channels (velocity carried over) share one arbitration | leading | the same recipes in three kinds of consumer; a capability matrix against GSAP and Rive |
| Plans an agent can inspect and control: any intermediate frame reviewed without a GUI | for what comes next | an agent finds and fixes an animation problem from process frames |
| General morphs across representations (properties, contours, display-list fragments, glyphs) | leading | against Manim's and Lottie's equivalents |
| Composable: leaves, compositions and recipes are one kind of value; typed channels users extend; composition rules checked at preparation | leading | expressiveness and diagnostics against GSAP and Framer Motion |

Convergence (plan §6) brings the repository's other animation implementations here: the UI's `Matter`/morph frame capabilities become a `visual` representation, `Set` becomes the interactive mode of one Engine per host.

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
`hanim.core` is at [core](core) for minimal consumers.
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
hanim = { path = ".../modules/projects/hanim" }
# Alternatives to the line above:
# hanim = { path = ".../modules/projects/hanim", default-features = false, features = ["effect"] }
# hanim = { path = ".../modules/projects/hanim", features = ["render-vector"] }
# hanim = { path = ".../modules/projects/hanim", features = ["control"] }
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
performed. `PreparedObjectAnimation::to()` is empty for a computed property target.

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
`PreparedObjectAnimation` remains the same compatible type. Its `sample_source`
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
reserves playback buffers. A morph is a recipe like any other:
`play(anim::morph(object, target).with_effects(effects).options(options))`.
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

**The recipe library (L6).** Forty reusable animations are public ordinary `Program` factories in `hanim::anim` — what a site, a video or a game composes with sequences and parallels. Each lowers at authoring time to relative property writes from the actual pose (so any object, or one part of it, takes it from wherever it is), the group opacity effect, the stroke's trim or the squash channel; no hidden state, clock or runtime.

| Kind | Recipes (defaults) |
|---|---|
| Entrances — from a pose of their own to where it stands, faded in | `fade_in` (0.3 s), `rise_in(distance 24)`, `sink_in(24)`, `slide_in(from {−40, 0})`, `pop_in` (from 0.3 of its size, `out_back`), `zoom_in` (from 1.4), `spin_in(−1.2 rad)`, `unfold_in` (its height from nothing), `write_in` (its stroke drawn along, `trim_end` 0 → 1), `drop_in(height 160)` (dropped onto its place, bouncing) |
| Exits — from where it stands to a pose of their own, faded out | `fade_out` (0.25 s), `rise_out(24)`, `sink_out(24)`, `slide_out(to {40, 0})`, `pop_out` (`in_back`), `zoom_out`, `spin_out(1.2 rad)`, `fold_out`, `unwrite` (its stroke undrawn, `trim_start` → 1) |
| Attention — ends where it began | `pulse(factor 1.08)`, `shake(amplitude {8, 0})`, `wobble(0.12 rad)`, `bounce(height 28)` (two hops, the second a third as high), `flash(times 2)`, `heartbeat(1.15)`, `rubber_band`, `swing(0.26 rad)` (each swing less), `tada`, `nudge(by {8, 0})` |
| Idle loops — `count` courses of `period` (forever by default) | `bob(amplitude 6, 2.4 s)`, `breathe(1.04, 3 s)`, `spin(1.2 s)`, `sway(0.08 rad, 2.4 s)`, `orbit(radius 16, 2 s)`, `blink(1 s)` |
| Moves | `arc_to(position, bend 0.3)`, `swap(a, b, a_to_b, bend 0.4)` (passing each other on either side), `reorder(objects, offsets, step 0.05 s)` (one after another), `move_through(waypoints)` |
| Bodies | `drop(part, offset)` (falls, squashes on landing, bounces lower each time) |

Every recipe takes `anim::RecipeOptions`: `duration` and `easing` (each recipe's own when unset) and an optional stable `part` id (empty: the whole object; `write_in`, `unwrite` and `drop_in` name the part with the stroke or the squash). Rotation and scale use the part's local origin. Bad values (infinite distances, a zero factor, a non-positive period) are refused at preparation with the usual typed errors. `tests/program/recipes_test.cpp` samples every recipe's whole course: where it starts, its middle, where it ends; loops by count; moves by track.

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
The definitions live in `src/program/feedback.cpp` and `src/program/recipes.cpp` because they
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
the original cause beneath it. Positive durations too small to retain each leg are
refused rather than silently turning a phase into an instantaneous step.
This does not change valid feedback pixels or require another error transport.

## Timed translation paths

`anim::move_through(object, waypoints, options)` returns an ordinary Program.
Each `MoveWaypoint` supplies `at` (time from the start of this recipe), `offset`
(from the actual starting pose in the selected part's parent space), and the
incoming leg's `easing` and `PointPath`. Time zero and offset `{0,0}` are implicit.
`MoveThroughOptions::part` selects a stable part; empty selects the root.

```cpp
using namespace std::chrono_literals;
auto route = hanim::anim::move_through(card, {
    {300ms, {14, -8}, hanim::ease::in_out_sine, hanim::PointPath::bend(.15)},
    {700ms, {-12, -6}, hanim::ease::in_out_sine, hanim::PointPath::bend(-.12)},
    {1200ms, {0, 0}, hanim::ease::in_out_sine, hanim::PointPath::bend(.15)}});
auto animation = hanim::composite(hanim::parallel(route,
    hanim::anim::wobble(card, .04, {.duration=1200ms, .part="title"})))
    .with_effect(hanim::fx::blur(0, 3));
auto playback = hengine.play(animation); // Inspect the normal typed result.
```

The span and initializer-list overloads copy the supplied descriptions into
owned leaves. They lower to relative translation sequences; compilation,
actual prior source poses, channels/conflicts, budgets, effects, inspection,
retargeting and sampling keep their existing owners. No target object is
fabricated or changed. Other parts/channels are preserved, and the route can
follow a prior move or run alongside rotation/scale. Each waypoint becomes an
author node, so the existing preparation node/segment/channel budgets apply.

Times must be finite, positive and strictly increasing. Repeated offsets with
later times express holds. An empty path, invalid ease/path, nonfinite offset
or unrepresentable difference returns an invalid Program, not a partial route.
Nested animation failures retain the offending waypoint index in `animation.clip`;
time/ease failures retain their normal typed code. Object-dependent overflow
still belongs to normal preparation. As elsewhere, no trap-safe arithmetic or
bitwise round-trip guarantee is claimed for all numeric inputs.

This is explicitly timed interpolation, not automatic arc-length speed,
orientation following or collision/path planning. Geometric distance-based
sampling remains the existing `MotionPath` facility; this factory does not
copy its geometry implementation. Per-leg curves use the existing `PointPath`.
Leg boundaries are position-continuous within normal numeric precision;
velocity continuity depends on the chosen times, curves and easing. A return
to `{0,0}` can close the route; a single zero-length leg remains a hold.

For headless review, inspect author nodes/channels, sample each waypoint and
multiple times inside every leg, replay in reverse, then capture actual pixels
with the selected renderer. Check unchanged parts, scoped effects and actions
at the moved position. The site's “Follow a path” selection consumes this exact
factory and supports its existing semantic seek controls. One path facility
and its parameter variations do not establish the complete 40/20 catalog.

## Group opacity as a composable image effect

```cpp
auto animation = hanim::composite(route)
    .with_effect(hanim::fx::opacity(1, .35f));
auto playback = hengine.play(animation);
```

The same effect works with an ordinary leaf's `with_effect`. It scales the
composed image once; it does not independently fade each fill or alter
transforms. The shared vector renderer supplies CPU and GPU implementations,
including exact identity at amount 1 and transparent output at 0. Renderer
binding validates the 0..1 range; generic PreparedEffects still owns finite
parameter interpolation without acquiring a renderer dependency.

Opacity changes presentation, not application visibility, layout, hit geometry,
focus, permissions or business state. A host that wants an invisible item to
stop interacting must apply its ordinary visibility/input policy. A UI image
scope covers the selected binding root's captured subtree; naming a part in
an unrelated property animation does not turn that image scope into a per-part
mask. Compose or select separate roots when that is the intended boundary.

Headless review: prepare the actual scope, sample endpoints and intermediate
times, capture through the authorized renderer and compare reverse replay.
Check overlapping content, translucent input, other layer opacity/blending,
clipping and DPI. A backend without image effects must report its limitation;
this does not introduce an automatic TUI equivalent or a new visibility policy.


## Timeline waits and delays

```cpp
using namespace std::chrono_literals;
auto animation = hanim::parallel(
    hanim::sequence(
        hanim::delay(200ms, hanim::anim::move_by(card, {18, 0}).duration(300ms)),
        hanim::wait(400ms),
        hanim::anim::move_by(card, {-18, 0}).duration(300ms)),
    hanim::delay(500ms, hanim::anim::wobble(title, .04, {.duration=400ms})));
auto playback = hengine.play(animation); // Check the expected result as usual.
```

`wait(duration)` is a pure timeline interval, not a blocking sleep, callback,
object animation, ownership release or permission grant. `delay(duration, child)`
is shorthand for `sequence(wait(duration), child)`. Both compose with nested
sequences, parallel/stagger branches and image scopes, use the existing player
clock and support arbitrary-time seek/reverse. A wait-only program is valid and
has zero object tracks. It neither draws nor freezes any external object.

Inside a sequence, the previous animation's existing endpoint fill remains
visible through the gap; a leading delay retains the following track's source
pose. Independently scheduled channels keep advancing. Existing track ownership
and conflict rules remain in force across a playback; waits do not allow an
unrelated playback to acquire that object's channels. A containing image scope
keeps interpolating its parameters throughout its duration, including waits.

Duration must be finite and nonnegative. Zero waits preserve author-tree order;
empty child programs remain invalid. Preparation rejects time overflow or a
positive duration too small to advance its absolute start. Waits count against
node/depth budgets but add no segments, geometry, properties or effect payload.
`Program::wait_duration()` and prepared node metadata expose the interval for
inspection. Prepared program contract version is 6, with existing kind values
preserved and `ProgramKind::wait` appended. Rebuild module consumers.

For headless review, sample before/during/after each interval and around branch
boundaries, then replay backwards. A paused object and an independently moving
part must be distinguishable in actual frames. The website note's “Move, pause,
return” recipe uses this API, including the public UI playback adapter; no page
clock or separate interpolation code is involved.


## Finite child repetition

```cpp
using namespace std::chrono_literals;
auto step = hanim::sequence(
    hanim::anim::move_by(object, {8, 0}).duration(200ms),
    hanim::wait(100ms));
auto animation = hanim::sequence(
    hanim::repeat(3, step),
    hanim::anim::move_by(object, {-24, 0}).duration(300ms));
auto prepared = hengine.prepare(animation);
if (prepared) {
    auto playback = hengine.play(*prepared);
    // Check playback, then use the normal seek/pause/reverse/release controls.
}
```

Repetition is finite **serial composition**, with the same stage-source rules
as writing the child three times. Each relative move starts from the previous
actual endpoint, so three +8 moves produce +24. Repeating `move_to(..., {8,0})`
reaches the absolute target and stays there on later iterations; no hidden
reset occurs. Use explicit snapshot sources when resetting is intentional.
Count is a positive `std::size_t`; zero fails with `invalid-repeat`.

A child can be a leaf, sequence, parallel, stagger, wait, effect scope or another
repeat. The authored value retains one child regardless of count. Preparation
expands only within existing node/depth/segment/scope/resource budgets, then
ordinary channel sampling executes the result. No host loop, callback or second
clock runs per frame. Preparation size is proportional to the expanded graph;
large repetitions can legitimately exceed budgets and return an error.

Place an image scope **inside** repeat to restart its explicit effect endpoints
for each iteration, or **outside** to interpolate across the whole duration.
A discontinuous effect restart is not automatically blended. The global player's
reverse/seek apply to the complete prepared result; finite repeat does not add
an infinite or alternating-loop policy.

Inspect `prepared->nodes()` for each repeat's count and expanded child root's
`iteration`, parent, start/duration and subtree range. Contract version 7 adds
these fields and preserves existing enum values. For headless review, sample
inside every iteration and around its boundaries, then seek backwards through
the same times. The website note's “Three steps and back” combines repeat with
independent part feedback, image effects and real editable UI using this API.


## Following a shared distance path

```cpp
using namespace std::chrono_literals;
hanim::MotionPath path;
path.move_to({0,0}).cubic_to({28,-18},{28,18},{0,0})
    .cubic_to({-28,-18},{-28,18},{0,0});
auto travel = hanim::anim::follow_path(object, std::move(path))
    .duration(1200ms).easing(hanim::ease::in_out_sine);
auto animation = hanim::composite(travel).with_effect(hanim::fx::opacity(1, .7));
auto playback = hengine.play(animation); // Check the expected result.
```

`follow_path` consumes the existing `hanim::MotionPath` distance lookup, without
lowering the curve to timed straight-line waypoints or adding a page sampler.
The selected root or named part moves by `path(u) - path(0)` from its actual
stage source, in its parent's coordinate system. Use the optional
`FollowPathOptions::part` for a child. Position is the only claimed channel;
rotation, scale, other parts and image scopes can be independently composed.
Closed routes work even when their endpoints coincide. This does not rename
objects, change UI state, orient an object to the tangent, or perform pathfinding.

`MotionPath` uses its existing **32 chord samples per cubic segment** to estimate
arc length. Linear ease gives approximately uniform local-distance motion;
nonlinear ease changes that distance progression, and parent transforms can
change world-space speed. This is not exact arc length, an adaptive error-bound
solver, or a universal high-precision representation. Existing `at(u)` still
provides position and heading; position-only sampling shares its lookup and
avoids calculating an unused heading.

A `TranslationRoute` owns an immutable copy and can be shared across multiple
bound animations through the overload taking that type. Ordinary MotionPath
arguments are frozen when the descriptor is created; later caller edits do not
change it. The low-level `PropertyTargets::route` is mutually exclusive with
position/offset. Empty, nonfinite or disconnected paths are refused at prepare;
continuous zero-length paths are valid stationary routes. Translation PointPath
modifiers add their existing endpoint-chord displacement to the route; closed
endpoint chords therefore have no bend displacement. Geometry deformation
modifiers are refused consistently by single-route and grouped property plans.
Image effects continue through their normal scope pipeline.

`MorphOptions::max_route_segments`, `ProgramLimits::max_total_route_segments`
and `EngineLimits::max_total_route_segments` bound retained route uses at their
respective levels. Counts are conservative logical uses, not unique allocation
bytes: sharing a frozen path does not bypass a budget. Zero disables routes at
that level. `route_segment_count()` on prepared object/program plans exposes
this cost; Engine play/retarget/release account for it transactionally. Preparing
and authoring may allocate; warmed route sampling uses immutable tables and
caller-provided buffers. Prepared program contract version is 8; rebuild module
consumers. No UI, renderer or additional package becomes a mandatory dependency.

For review, sample multiple intermediate times and both closed lobes, then seek
backwards. The website note's “Curve tour” uses this public API with separately
animated parts, image style and real input. Core reference tests that read files
use `mcpp test -p hanim.core --target wasm32-emscripten --profile wasm-test`;
this Node-only test profile is separate from the site's publishing profile.
