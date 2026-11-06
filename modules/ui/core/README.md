# openmui.core

The foundation of the openmui runtime (module `openmui.core`):

| Partition | Contents |
|---|---|
| `:base` | the spec's value types (`Value`, `Rect`, `Role`, `Effect` …) and `seconds` from hanim |
| `:diag` | Thin compatibility export of independent [mlibs.log](../../base/utils/src/log/README.md): `info` / `warn` / `error`, `std::source_location`, `invariant`, sinks and history |
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

## Carrying UI authority into independent engines

`omui::to_control_invocation(const Invocation&)` converts an already host-bound
UI call into `mlibs::control::Invocation`. It retains the same Access/session
objects, actor attribution, answer, origin operation and dry-run state. Domain
arguments remain separately validated typed parameters; UI `Value` payloads
are not copied into the shared control contract.

Inside an admitted Runtime verb callback, carry its actual current call:

```cpp
const auto ui_call = runtime.current_invocation();
if (!ui_call) return std::unexpected("Missing UI invocation");
auto native_call = omui::to_control_invocation(*ui_call);
if (!native_call) return std::unexpected("Unbound UI authority");
auto reply = controller.seek_progress(*native_call, playback, progress);
// Inspect admission/domain failure and executed before publishing a result.
```

A remote call without bound Access returns `unbound_access`; without a Session
it returns `missing_session`. Trusted local host calls may omit both. Conversion
does not authenticate request JSON, grant permission, create/renew a Session,
enter a scope, or use the legacy global fallback. The destination Controller
enters its own scope and rechecks current policy, including revocation.

For the UI `invoke` entry followed by Hanim `animation.seek-progress`, operation
restrictions must allow both. The origin remains `invoke`; conversion never
aliases transport permission into domain permission. Share the actual Access
between hosts. For worker scheduling, copy the converted call while the bound
UI callback is active, then re-enter it at execution; do not replace a missing
later UI context with an empty local call. Contexts are serial and do not
automatically inherit across these two Invocation representations.

The [independent UI consumer](../../../tools/ci/omui-hanim-consumer/src/main.cpp)
drives the same public Hanim Controller through UI verbs and a real mouse
button, checks both permission boundaries and revocation between them, and
samples 61 actual rendered frames. This adapter adds no Hanim dependency to
UI core; it is usable by other independent domains importing `mlibs.control`.
