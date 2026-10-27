# openmui-view

The retained and immediate view runtime (plan §3–§5):

- **elements** (contract ②): `Prop<T>` accepts a value, a signal (two-way
  where the property is bindable) or a computed value; `fmt` and `derive`
  build reactive text and values; `when`, `each` and `dynamic` are regions
  rebuilt when what they read changes, reconciled by key;
- **components**: a C++ class whose function table `component_type<C>()`
  derives at compile time (paint, measure, events, actions, semantics …);
- **theme tokens**: colours, text styles, spacing, radii, shadows, motion;
- **the runtime**: nodes with structural and explicit ids (S-8), fine-grained
  bindings, transitions on animation channels (enter, exit with S-4), timers
  on the injected clock, incremental layout, painting into a display list with
  node markers, hit testing through transforms, pointer capture, hover,
  focus and Tab order, contract key bindings, actions under S-1/S-2 with an
  audit journal, and each node's semantics;
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
- **immediate mode** (`omui::immediate`, `Ui`): ids from the id stack and
  call site, builds only when something happened;
- **the frame is a pipeline** (D-66): eight stage plugins
  (`openmui-view.timers` … `.debug`) over the frame's resources (`frame`,
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
  the component. The motion is a pure function of time: it seeks, pauses and steps, and respects
  reduced motion. It costs about 90 ns per glyph per frame (`text/glyph-motion`).
- **pixel effects** (D-68): any node's whole drawing, children included, in an effect layer of
  `render::effects()`: dissolve, wipe, reveal, displace, noise, chromatic, blur, pixelate, particles,
  or one a program registers. The `pixel_effect` property is a map with the effect's name and its
  parameters by name. With a `duration` (and `ease`, `delay`, `reverse`) the runtime drives `progress`
  from the time it was set. The effect is seekable, and the node is `animating` until it ends.
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

Depends on `openmui-core`, `-layout`, `-render`, `-text`, `-image`, `-anim`,
`-geom`, `-memory`, `-trace` and `-pipeline`. Paint is deterministic: the reference scene renders to
the same bytes with GCC and LLVM, on Linux, macOS and Windows.
