# components

openmui's components (`openmui.ui.*`), contract-first: each `*.contract.omui.toml` generates the C++ partition and the TypeScript, Python and Lua bindings; the behaviour is C++ over the runtime's public API.

| Package | |
|---|---|
| [base](base) | The core components: button, toggle, checkbox, slider, text box, … |
| [canvas](canvas) | The infinite canvas: a columnar scene, a grid index, a double-precision camera, tiers of detail; its behaviours (selection, dragging, snapping, minimap, statecharts) inside it |
| [markdown](markdown) | Markdown as semantic nodes: links with external effects, tasks, code, tables, streaming |
| [liquid-glass](liquid-glass) | Thick transparent material over its backdrop, and groups that merge like drops |
| [timeline](timeline) | The timeline every editor shares: tracks of clips, a ruler and a playhead; moves, trims, splits and removals by pointer, keys or agents, every edit undone exactly |
| [window](window) | The framework's own window frame: shadow, edge, title bar, caption buttons |
