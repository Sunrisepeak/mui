# components

openmui's components (`openmui.ui.*`), contract-first: each `*.contract.omui.toml` generates the C++ partition and the TypeScript binding; the behaviour is C++ over the runtime's public API.

| Package | |
|---|---|
| [base](base) | The core components: button, toggle, checkbox, slider, text box, …; beside them, modules of their own in the same package: the framework's window frame ([`omui.ui.window`](base/src/window): shadow, edge, title bar, caption buttons) and liquid glass ([`omui.ui.liquid_glass`](base/src/liquid_glass): thick transparent material over its backdrop, groups that merge like drops) |
| [canvas](canvas) | The infinite canvas: a columnar scene, a grid index, a double-precision camera, tiers of detail; its behaviours (selection, dragging, snapping, minimap, statecharts) inside it |
| [markdown](markdown) | Markdown as semantic nodes: links with external effects, tasks, code, tables, streaming |
| [timeline](timeline) | The timeline every editor shares: tracks of clips, a ruler and a playhead; moves, trims, splits and removals by pointer, keys or agents, every edit undone exactly |
