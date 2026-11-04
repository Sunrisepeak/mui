# openmui.view

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
