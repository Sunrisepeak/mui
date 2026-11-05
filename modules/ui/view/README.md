# openmui.view

The implementation namespace is `omui`; `openmui.view` remains the compatible
package and main import. Its optional `hanim` feature adds the public
`import omui.hanim;` adapter in this same package.

The retained and immediate view runtime (plan §3–§5):

- **elements** (contract ②): `Prop<T>` accepts a value, a signal (two-way
  where the property is bindable) or a computed value; `fmt` and `derive`
  build reactive text and values; `when`, `each` and `dynamic` are regions
  rebuilt when what they read changes, reconciled by key;
- **components**: a C++ class whose function table `component_type<C>()`
  derives at compile time (paint, measure, events, actions, semantics …);
- **theme tokens**: colours, text styles, spacing, radii, shadows, motion;
  and **theme scopes** (D-89): a subtree with a theme and effect rules of its
  own (`Runtime::set_scope(name, theme, rules)`; a node joins with its
  `theme_scope` property, its descendants and the overlays it opens with it).
  The nearest scope wins, whole. A component's hooks are told their scope's
  theme by `Services::theme()`; "window" rules apply to the node that joins;
  `crossfade(length, scope)` fades that area only;
- **path motion**: the `motion` property accepts reusable `vertex_path` data,
  e.g. `{"use":"morph","curve":"motion.slow","vertex_path":{"bend":0.22}}`.
  The runtime passes it to the public `hanim::PointPath` operator after path
  matching; components still paint only their endpoint states. A bend is a
  signed fraction of each point's endpoint displacement, not a pixel distance.
  Explicit `{"control1":[along,normal],"control2":[along,normal]}` offsets allow
  asymmetric cubic paths; `null` or `"linear"` resets an inherited modifier.
  Nested fields are checked strictly, with finite controls bounded to ±1e6.
  This affects matched fill/stroke geometry, not text, images or hit contours.
  Reduced transitions use their short default spatial interpolation; off and
  system reduced motion also retire already-held morphs. Timing, render styles
  and image effects remain separate controls;
- **the runtime**: nodes with structural and explicit ids (S-8), fine-grained
  bindings, transitions on animation channels (enter, exit with S-4), timers
  on the injected clock, incremental layout, painting into a display list with
  node markers, hit testing through transforms, pointer capture, hover,
  focus and Tab order, contract key bindings, actions under S-1/S-2 with an
  audit journal, and each node's semantics;
- **history and transactions** (S-12, S-13): an action's implementation gives
  its inverse (`ActResult::undo_by("set_value", {value: old})`); the runtime
  keeps a history per scope (the window, or a node with `undo_scope`), whose
  node offers `undo` and `redo` as actions (Ctrl/Cmd+Z, Shift for redo); an
  irreversible or external action is a barrier. `begin_transaction` …
  `commit_transaction` makes one step of history, `rollback_transaction`
  takes it all back. `Invocation::dry_run` checks a call as it would be
  performed and answers its outcome (a component's optional `preview` adds
  the value after), changing nothing;
- **primitives**: box, column, row, stack, grid, spacer, text, image, scroll
  view, painter, and their builders with designated-initializer options;
- **models**: named, keyed rows that lists show and scripts and agents push
  to; a revision signal makes readers follow;
- **overlays** (menus, dialogs, tooltips, popups): laid out from the root,
  placed against an anchor, painted on top, light dismiss, modal focus trap
  and restore; **composite** components that build their own children; a
  `laid_out` hook for measurement-driven adjustments; virtualisation hooks
  (`virtual_items`, `reveal`) so a virtualised tree answers queries over all
  of its content (S-7);
- **for view documents and other generic builders**: `registered_types()`, the runtime's own
  `runtime_properties()` and `runtime_events()`; `ComponentType::set_item` (a list takes the view of
  a row from whoever builds it) and `create_in` (a type made at run time carries its context);
  `verb_args`, the node's own arguments to its activation's verb, which a caller cannot override;
- **immediate mode** (`omui::immediate`, `Ui`): ids from the id stack and
  call site, builds only when something happened;
- **the frame is a pipeline** (D-66): eight stage plugins
  (`openmui.view.timers` … `.debug`) over the frame's resources (`frame`,
  `tree`, `layout`, `bounds`, `display`); `runtime_pipeline(Stages)` gives
  H0, H1 and H2; `RuntimeOptions::pipeline` / `plugins` add your own stages;
  `frame_pipeline()` and `stage_plugins()` change it between frames;
- **time control** (F4): `pause_time`, `set_time_rate`, `step_time`,
  `seek_time`, `release_time` map the host's clock to the time the
  application sees — timers and animations alike; seeking re-evaluates every
  live channel at the new time, so a finished transition plays again when
  time goes back (settled property transitions keep their channels for it);
- **glyph motion** (D-68): any node's text, animated glyph by glyph, grapheme, word or line. It is the
  `glyph_motion` property (`animate_glyphs(element, GlyphMotion::rise())`, or a value an agent sets).
  After a component paints, the runtime styles the glyphs it recorded at the frame's time, so every
  component's text can be typed out, raised, gathered, scattered or set to a wave with no change to
  the component. The text's decorations go with it: a fill no taller than a line, drawn under or
  behind a paragraph's glyphs (an underline, a strikethrough, an inline code's background), moves by
  those glyphs' mean offset and fades with their mean opacity. The motion is a pure function of time: it seeks, pauses and steps, and respects
  reduced motion. It costs about 90 ns per glyph per frame (`text/glyph-motion`).
- **pixel effects** (D-68): any node's whole drawing, children included, in an effect layer of
  `render::effects()`: dissolve, wipe, reveal, displace, noise, chromatic, blur, pixelate, particles,
  or one a program registers. The `pixel_effect` property is a map with the effect's name and its
  parameters by name. `animate: {parameter: [from, to]}` with `duration` prepares named scalar
  channels through public `hanim.effect`; `ease`, `delay` and `reverse` share the runtime clock.
  The legacy duration-only form still drives `progress`; effects without that parameter require
  explicit channels. The effect is seekable, with exact endpoints. Reduced/off motion selects
  the terminal value without scheduling a continuing animation. Style rules may declare
  `usage: "appearance"` to retain base material on disabled controls; the default `"feedback"`
  suppresses decoration there. Disabled controls never select hover/press/focus variants.
  A declared identity omits the UI effect wrapper, avoiding another application of ancestor
  antialiasing clips; the node's ordinary opacity layer remains intact.
- **observability** (D-59): every frame times its phases (timers, input,
  regions, animations, layout, bounds, paint) into `FrameStats`, kept for the
  last 240 frames (`frame_history()`) and traced as `openmui.trace` spans;
  frames slower than `DebugOptions::slow_frame_ms` log a warning in the
  `frame` category (at most once a second); `inspect(node)` returns what the
  runtime knows of a node — type, contract, source line, props, bindings,
  handlers, state, layout style and boxes; `set_debug()` turns on overlays
  painted over the frame: node bounds (a hue per depth), repaint flashes, the
  hovered node's margin / border / content boxes with its path and source,
  and a frame-time HUD (mean, p95, node counts).

Depends on `openmui.core`, `-layout`, `-render`, `-text`, `-image`, `-anim`,
`-geom`, `-memory`, `-trace`, `-pipeline` and `hanim.effect`. Paint is deterministic: the reference scene renders to
the same bytes with GCC and LLVM, on Linux, macOS and Windows.

For a local chart, set `pixel_effect` on its painter, with labels and controls
as siblings when their drawing must remain unchanged:

```json
{"name":"pixelate","size":8,"device_pixels":1,"duration":0.4,
 "animate":{"amount":[0,1]},"ease":"linear"}
```

An effect wraps everything its node paints; a nested theme reset does not
cancel an ancestor's image filter. This scalar bridge does not provide a
general typed-channel mixer, automatic retargeting or a complete pixel style
pack. Component paint-part isolation remains a separate capability.

Minimal headless workflow: expose normal component actions for the controls,
discover their stable IDs through openas, invoke the action, inspect its result,
and capture the selected region using the host's real renderer. Use runtime
time control for deterministic start/middle/end and replay checks. The site
feature test exercises `act` and PNG `capture` through `agent::Protocol`
without a window. Invalid animation names, endpoints or timing are diagnosed;
the rejected effect is not installed. Cancel/remove it through the owning
property; no hidden player or GUI lifecycle is needed.

Animation acceptance samples the process, not only its endpoints. The effect
animation suite renders 61 timestamps over one second at 2× DPI, checks every
interval's visual change and unchanged outside pixels, retains identity/hit/
focus, and replays all samples in reverse. It exports the full PNG sequence,
a lossless one-shot WebP (including one terminal sample interval), and an
eight-frame contact sheet under `openmui-tests/effect-animation`. The sheet's
times, left-to-right then top-to-bottom, are 0, .05, .15, .30, .50, .70, .90, 1s.
Structured output records every timestamp and pixel hash. This deterministic
sampling does not measure live frame pacing or replace human motion review.

## Optional Hanim presentation binding (0.1)

Enable `features = ["hanim"]` on the existing `openmui.view` dependency and
`import omui.hanim;`. This adds the public Hanim player and a native adapter;
ordinary view consumers retain the smaller existing dependency selection.
There is no separate adapter package, window or second animation clock.

```cpp
const std::array selection{
    omui::animation::Target{"card", card, omui::PresentationChannel::translation},
    omui::animation::Target{"title", title, omui::PresentationChannel::rotation}
};
auto binding = omui::animation::Binding::attach(runtime, selection, hengine);
if (!binding) return; // Inspect the typed failure and target index.
auto playback = hengine.play(hanim::parallel(
    hanim::anim::move_to(binding->object(), {100, 20}),
    hanim::anim::to(binding->object(), {.rotation = .12, .part = "title"})));
if (!playback) {
    const auto object = binding->object();
    binding->detach();
    (void)hengine.destroy_object(object);
    return;
}

// Explicit sampling on the owning thread; process every expected result.
if (hengine.seek_progress(*playback, .5)) {
    if (auto frame = hengine.frame(*playback); frame && binding->present(*frame)) {
        (void)runtime.frame({hanim::seconds(3), {}});
        const auto actual = runtime.presentation(card); // has_frame/applied/error
        // Inspect semantics and render runtime.display_list() without a window.
    }
}
const auto object = binding->object();
(void)hengine.release(*playback);
binding->detach();
(void)hengine.destroy_object(object);
```

The object represents **selected local presentation offsets**, initially
identity. It does not capture UI layout, glyph outlines or business state.
The first selected node is the rendered root; other selections are descendants
in that same rendered subtree. Any node type, including text, can be selected.
Unselected descendants inherit the group's transform; they are not pinned in
world space. Selected descendants compose their own offsets with ancestors.
An anchor is a normalized border-box point; translation, rotation, scale and
shear are independent leases. Disjoint writers may share a node with the same
anchor; overlapping writers return `occupied`.

`present` validates the whole input before publishing selected fields and
retains no EngineFrame spans. UI painting then commits these offsets around
complete node ranges, including text, local clips and input metadata. Ordinary
component feedback remains active. Unprojected paint stays the source of
automatic morphs, so a published offset does not get interpolated a second
time. IME caret queries use committed presentation. Anchored overlays follow
the projected target allocation without a second easing, including bounded
clips; their state/presence motion remains independent. Explicit
`placement: "animate"` keeps the authored placement animation instead.

Minimal SOP: supply the application's fonts/assets, resolve stable node IDs after mounting, attach after existing
flattened morphs settle, prepare/play through Hanim, sample and publish on the
UI thread, commit a paint frame, then inspect every target's status and actual
pixels. Sample intermediate times, reversal and interruption, and exercise
pointer plus Agent actions on the same production controls. A native library
call does not grant remote authority: expose application control through the
existing authorized openas endpoint. `Runtime::inspect` reports the selected
channels, pose and commit status; this adapter adds no new network endpoint.

Invalid/stale selection, busy first attachment, overlapping writers and
unsupported frames have typed failures. An invalid publication keeps prior
values. A later paint failure is reported in `PresentationState::error`;
accepted publication alone is not proof of a rendered result. Detaching
releases only that lease's channels. The Engine owns the created object:
release playbacks, detach, then destroy it; Binding destruction only releases
UI leases and is safe after Runtime destruction.

This first tier supports finite 2D local transforms and the explicitly prepared
image scopes below. It refuses cross-portal selections and geometry/material
frames rather than dropping their data. Historical crossfade snapshots remain captured images of their
original presentation. General constraint/hold-world solving, sampled-base
anchor tracking during unrelated layout morphs and geometry/3D adapters
remain separate work. Explicit time
sampling can inspect poses with UI automatic motion disabled; an application's
live driver must enforce its motion preferences and input policy. The optional
owning `Playback` below supplies that host transport without another engine.

The [independent consumer](../../../tools/ci/omui-hanim-consumer/src/main.cpp)
uses the real button/popover, shaped text, public Hanim composition, renderer
and Agent actions. Its [runner](../../../tools/ci/omui-hanim-consumer/run.py)
exports 61 explicit samples, 61 host-driven frames, 61 nested-effect frames and
61 public-feedback frames, with exact reverse
replay in native dev/release and optional Wasm/Node, without changing the
source workspace's lock.

## Native image scopes on live UI content

The same optional adapter accepts a single HObject's leaf effects and nested
`hanim::composite(...).with_effect(...)` scopes. The selected root supplies the
actual rendered content, including shaped text, child controls and their normal
state feedback. For example, pass this program to the owning `Playback`:

```cpp
auto animation = hanim::composite(
    hanim::anim::move_to(playback.object(), {24, 0})
        .with_effect(hanim::fx::blur(0, 3)))
    .with_effect(hanim::fx::tone({.amount=0}, {.amount=.2f}));
auto started = playback.play(animation); // Check the typed result.
```

With an externally owned Engine, call `hengine.prepare(animation)`, then
`binding.prepare(prepared)` before `hengine.play(prepared)`. Sample with
`hengine.program_frame(handle)` and `binding.present(frame)`. This carries the
plan identity and all scope samples. The legacy `EngineFrame` pose path refuses
image effects, including effects whose authored parameter list is empty;
it must not silently lose renderer defaults.

Hanim remains the only parameter/time evaluator. `hanim.render.vector` supplies
the same `PreparedImageEffects` name/default/range/generation binding used by
its standalone vector consumer. UI owns preallocated, copied layer parameters
and validates the complete pose/effect batch before committing. Renderer content
bounds capture displaced descendants, glyph overhang and nested filter output;
layout boxes do not crop the capture. Zero-strength UI image filters are omitted
when their provider declares exact identity. Local style is independently
composable; neither changes the underlying component data.

Image ownership is independent of translation/rotation/scale/shear. The native
`PresentationLease` can reserve an image channel on any selected node, with up
to 64 layers and 4,096 backend parameters per node. Cold `reserve_effects` owns
capacity; `publish(poses, effects)` copies spans atomically. An empty effects
argument preserves the current chain; an explicitly empty per-node chain clears
it. A later pure Hanim program clears its old filters and releases only an image
channel acquired by the adapter itself. Release/detach leaves other writers intact.

Current binding requires a CPU reference, no backdrop, no spatial inverse map,
and the effect provider's explicit `preserves_hit_geometry` declaration. Blur,
tone and pixelate declare this contract. Custom effects opt in only when they
retain control identity and location; unknown/deforming providers are refused.
Changing a provider generation requires rebind at a quiescent boundary. Publication
failure retains accepted values; paint-time provider invalidation is observable
as a presentation error and does not promise to reproduce the old shader output.
For an externally owned Engine, prepare the existing plan again on `Binding`
and publish a fresh `program_frame`. For the owning host, call
`playback.rebind()` as described below; do not restart the animation to refresh
its renderer bindings.

The Hanim image scope covers the selected **root**, regardless of which child
transform channels animate. Select text itself as another binding's root for a
text-only scope. Per-part scope IR, arbitrary cross-object/cross-tree compositing,
backdrop and spatial input mapping remain separate work. No new network authority
is added: expose controls and inspection through the application's existing
authorized openas endpoint. Headless review must render the real frame, check
presentation errors, sample the process and verify actual component actions.

## Optional owning playback host

Use `omui::animation::Playback` when one selected UI object owns its program;
use the lower-level `Binding` above when the application already owns a shared
Hanim Engine. Both use the same public Hanim programs, player and presentation
leases. The host adds no GUI, script, timer thread or separate interpolator.

```cpp
auto attached = omui::animation::Playback::attach(runtime, selection);
if (!attached) return; // Check typed binding/engine failure; busy can be retried.
auto playback = std::move(*attached);
auto started = playback.play(hanim::anim::move_to(playback.object(), {24, 0}),
                             {.alternate = true});
if (!started) return;
// Store playback for as long as the selected UI scene lives.
```

Install a native frame pipeline stage which calls
`playback.update({.visible=visible, .foreground=foreground, .interacting=editing})`
**after input/regions/layout and before bounds/paint**, on the UI thread.
Declare both `tree` and `layout` reads, the `tree` write, stage cost and trace
category, and the ordering edge into `bounds`. Check registration/build/update
results. This explicit integration is required: calling `update` before the
Runtime latches its frame clock would sample the previous time. The independent
consumer's `DriveStage` is a complete working example. A stopped, still-attached
host stage is idle and permits paint; a stale selection returns a typed error.

The transport uses Runtime time and Hanim's existing `TimeMap` (including
backward time queries and repeat direction). `pause`, `resume`, `reverse`,
nonzero signed `rate`, `seek`, `seek_progress` and `stop` control the same native
playback. A second `play` returns `occupied` until explicitly stopped.
`stop(restore)` publishes the initial program pose; `stop(hold)` retains the
last pose. Destruction releases the binding and demand along with its owned
Engine. No background work survives a destroyed Runtime.

After replacing an effect provider, call `rebind()` on the UI thread at a
quiescent boundary, before the next sample/paint. It rebuilds the renderer
bindings for the existing immutable plan and republishes its current sample.
Position, direction, rate, host clock mapping and pause/gate intent are retained;
success requests one frame, without implicitly resuming continuous playback.
This is a cold operation that may allocate, not a per-frame callback. Provider
replacement must not race preparation, sampling or rendering.

Check the returned `PlaybackFailure`: `no_program` means there is no current
plan, `stale` means the selection/Runtime no longer exists, and `binding` or
`engine` carries the underlying typed error. An incompatible provider is
refused without changing the accepted UI values or restarting transport.
Restore a compatible provider and retry `rebind()`, then render and inspect the
selected nodes. Old accepted parameters are not a lease on old provider code;
failure does not guarantee that the replaced shader can still draw the old
image. This API neither loads plugin binaries nor supplies concurrent registry
replacement. Pure property programs accept the same operation.

Hidden/background/interactive gates and Runtime reduced/off preferences
suspend automatic sampling, release continuous frame demand and retain the
visible pose. Returning from a gate starts there without catching up hidden
time. An explicit pause is not undone by a gate reopening. Explicit `seek`
pauses automatic transport and can still inspect any frame with automatic
motion disabled. A paused Runtime clock requests no busy frames; an explicit
Runtime time seek is still honored. This API does not select visibility or
focus policy on the application's behalf, nor does it implement a general
budget/representation downgrade controller.

`status()` reports the actual native position/rate/state, requested intent,
suspension reason, paused clock and frame demand; `plan()` exposes the immutable
Hanim plan. A publication remains distinct from successful paint: inspect the
selected nodes' committed presentation after a frame. Application openas
actions can call these same methods under their existing authorization; no
network endpoint or model dependency is added.

Headless SOP: load fonts/assets, mount and settle initial feedback, resolve
selection IDs, attach and install the stage, play, then drive Runtime frames at
known times. Render the display list, inspect semantics/presentation, and sample
the process forward and backward. Verify interruption, gates, default feedback
and actual control actions, then stop/detach on scene teardown. The site's
`component-motion` scene applies this flow to a real editable/copyable note,
with whole-card, independently selected parts and title-only recipes; recipe
details are exposed only on request.
