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
`-geom`, `-memory` and `-trace`. Paint is deterministic: the reference scene renders to
the same bytes with GCC and LLVM, on Linux, macOS and Windows.
