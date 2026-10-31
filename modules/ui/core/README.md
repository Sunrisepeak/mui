# openmui.core

The foundation of the openmui runtime (module `openmui.core`):

| Partition | Contents |
|---|---|
| `:base` | the spec's value types (`Value`, `Rect`, `Role`, `Effect` …) and `seconds` from hanim |
| `:diag` | `info` / `warn` / `error` with `std::source_location`, `invariant`, a replaceable sink — no macros |
| `:signal` | `Signal`, `Computed`, `Watch`, `batch`, `untracked`: push-stale, pull-fresh, glitch-free |
| `:verb` | `Verbs`: named operations with effects; S-1 enforced at registration, S-2 `needs-answer` at invocation; a journal |
| `:keys`, `:event` | keys, modifiers, chords (`Mod` = the platform's primary modifier); input events and their JSON form |
| `:display` | the display list (contract ⑤): paths, paints, gradients, strokes, clips, transforms, layers, glyph runs, node markers; content hashing |
| `:arena`, `:handle`, `:function` | the frame arena, generational handles and slot maps, `FunctionRef` |
| `:json` | JSON to `Value` through nlohmann.json's SAX parser, with line and column in errors |
| `:task` | the taskflow pool with deterministic chunking, cancellation, and the lock-free queue to the UI thread |

Tests: `mcpp test -p openmui.core`.
