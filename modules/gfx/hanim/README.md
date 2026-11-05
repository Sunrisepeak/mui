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
```

`render-vector` implies program/library/object/effect, not the player. A consumer
can turn defaults off and use it without Engine. Feature requests are additive
across dependency edges. The generated `hanim:selected` partition exports only
selected modules; disabled sources and dependencies are not compiled. There is
no runtime feature dispatch or separate authoring API.

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

`move_to`, `move_by`, `rotate_to`, `scale_to` and `fill_to` are five public
recipes. They accept live ObjectRefs or frozen Snapshots, the same duration,
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
