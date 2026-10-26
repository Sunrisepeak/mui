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
- **immediate mode** (`omui::immediate`, `Ui`): ids from the id stack and
  call site, builds only when something happened.

Depends on `openmui-core`, `-layout`, `-render`, `-text`, `-image`, `-anim`,
`-geom` and `-memory`. Paint is deterministic: the reference scene renders to
the same bytes with GCC and LLVM, on Linux, macOS and Windows.
