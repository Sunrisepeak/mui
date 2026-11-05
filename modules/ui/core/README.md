# openmui.core

The foundation of the openmui runtime (module `openmui.core`):

| Partition | Contents |
|---|---|
| `:base` | the spec's value types (`Value`, `Rect`, `Role`, `Effect` …) and `seconds` from hanim |
| `:diag` | Thin compatibility export of independent [openmui.log](../../base/log): `info` / `warn` / `error`, `std::source_location`, `invariant`, sinks and history |
| `:signal` | `Signal`, `Computed`, `Watch`, `batch`, `untracked`: push-stale, pull-fresh, glitch-free |
| `:verb` | `Verbs`: named operations with effects; S-1 enforced at registration, S-2 `needs-answer` at invocation; a journal |
| `:access` | UI compatibility adapters and Value serialization over independent [mlibs.control](../../base/control); policy, session and openas decisions have one owner |
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

Independent engines import `mlibs.control` directly. A trusted host can pass a
shared `mlibs::control::Access` to `AgentAccess`, and use `control_binding()` and
`AgentSession::control_session()` to share the actual issuer with another domain.
These native handles never come from request JSON. The existing UI aggregate
types, operation envelopes and `AgentPolicyState::to_value()` remain available;
`InvocationContext`/`InvocationScope` alias the shared context mechanism. C++
module consumers rebuild; this extraction does not promise binary compatibility.
An omitted operation now falls back to the verb name when applying filters.
