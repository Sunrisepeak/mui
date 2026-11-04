# openmui.core

The foundation of the openmui runtime (module `openmui.core`):

| Partition | Contents |
|---|---|
| `:base` | the spec's value types (`Value`, `Rect`, `Role`, `Effect` …) and `seconds` from hanim |
| `:diag` | `info` / `warn` / `error` with `std::source_location`, `invariant`, a replaceable sink — no macros |
| `:signal` | `Signal`, `Computed`, `Watch`, `batch`, `untracked`: push-stale, pull-fresh, glitch-free |
| `:verb` | `Verbs`: named operations with effects; S-1 enforced at registration, S-2 `needs-answer` at invocation; a journal |
| `:access` | instance Agent policies, intersecting developer/user/session restrictions, trusted development sessions and `openas::decide` |
| `:keys`, `:event` | keys, modifiers, chords (`Mod` = the platform's primary modifier); input events and their JSON form |
| `:display` | the display list (contract ⑤): paths, paints, gradients, strokes, clips, transforms, layers, glyph runs, node markers; content hashing |
| `:arena`, `:handle`, `:function` | the frame arena, generational handles and slot maps, `FunctionRef` |
| `:json` | JSON to `Value` through nlohmann.json's SAX parser, with line and column in errors |
| `:task` | the taskflow pool with deterministic chunking, cancellation, and the lock-free queue to the UI thread |

Tests: `mcpp test -p openmui.core`.

Agent policy keeps `AgentMode` separate from an effect cap. Bind an
`AgentAccess` instance with `Verbs::set_agent_access`; a trusted native host
creates its `AgentSession` with `open_session`. Development preauthorization
belongs to that session and instance. Request text cannot mint it, and a
revoked session does not renew its own grant. S-10 still requires actual
human presence for person-only actions.

`AgentRestrictions` narrow enabled state, input, cap and operation names at
developer, user and session levels. Exact names, `prefix.*` and `*` are
supported; an explicit empty operation list denies all operations. Resource
filters, expiry and persistent user configuration remain separate work.
`InvocationContext` carries remote authority through nested callbacks; hosts
can share an instance context with their `Verbs`. Unbound low-level
callers retain the legacy `agent_access()` fallback.
