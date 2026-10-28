# modules/ui

openmui: the reference implementation of the [openmui](../../spec) specification. A view, its semantics and its pixels come from one tree.

Depends on: [base](../base), [gfx](../gfx), [content](../content), [spec](../../spec). Never on a game engine.

| Package | |
|---|---|
| [core](core) | Signals, verbs, input events, JSON, diagnostics, tasks; who may do what (openas) |
| [expr](expr) | Expressions: a CEL-compatible parser, printer and evaluator for bindings |
| [view](view) | The retained view runtime: elements, components, theme tokens, frames, input, actions, windows |
| [semantic](semantic) | The semantic tree: snapshots, deltas, queries, actions, waits, checks, recording |
| [agent](agent) | The agent endpoint: `openmui.tree` over local IPC, JSON lines, envelopes |
| [viewdoc](viewdoc) | View documents: checking, loading, reloading, component documents, the schema |
| [theme](theme) | Themes as data (`*.theme.omui.toml`); the default, compact and style packs inside it |
| [native](native) | The native-module registry: contract-typed C++ functions scripts call; the official modules inside it |
| [abi](abi) | The C ABI (`openmui.h`) |
| [player](player) | The player: an application's views, components and scripts, run and reloaded in place |
| [reflect](reflect) | C++26 reflection: forms from structs (GCC 16) |
| [host](host) | Hosts: headless and the SDL3 desktop |

Start with [view](view/README.md).
