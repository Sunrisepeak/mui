# mlibs.control

Independent authorization and call authority for UI and standalone engines.
Import `mlibs.control`; use `mlibs::control`. The only runtime dependencies are
openas 0.2 and the existing independent logging and tracing libraries. There is
no UI/spec Value, renderer, network transport, model or global Access instance.

The implementation is extracted from `openmui.core`: policy intersections,
session issuance/revocation, grant epochs and nested call binding have one owner.
UI `AgentAccess`/`AgentSession` remain compatibility adapters and serialize UI
values there. This independent package lets a hanim-only consumer opt into the
same controls without importing UI. Policy/session/context stay together in
this package; individual operations do not each become a package.

```cpp
import std;
import mlibs.control;

auto access = std::make_shared<mlibs::control::Access>(
    mlibs::control::policy_for_profile("dev"));
auto session = access->open_session(); // trusted host, never request JSON
auto decision = access->decide("animation.seek", openas::Effect::reversible,
    false, {openas::Kind::agent, "reviewer", {}}, session.get(), "animation.seek");
// Invoke the domain's own operation only when decision == openas::Decision::run.
```

Only exact profile `dev` selects development automatically. A trusted bound
development session gets preauthorization, still intersected with developer,
user and session restrictions and the host's actual capabilities. Release and
unknown profiles use dual-user policy. Remote claims never establish physical
presence, and a delegation string cannot substitute for a session handle.

`Policy` and `Restrictions` use openas effect caps. Operation lists accept exact
names, `prefix.*` and `*`; a present empty list denies all. The host supplies the
operation name to `decide`, including read/capture/export requests. If omitted,
it falls back to the verb name instead of bypassing operation restrictions. `PolicyState`
and its borrowed `OperationPolicy` expose the same filter to adapters.
Define whether these names are transport operations (the existing UI protocol)
or domain operations (the hanim CLI consumer); do not mix taxonomies. A domain
provider checks the operation currently entered, including nested callbacks;
`Invocation::operation` retains the initiating context and is not a substitute
for identifying that current domain operation.

`Invocation` contains authority metadata, not domain payloads/results. Bind it
with `bind_invocation` and an instance-owned `InvocationContext`; `InvocationScope`
keeps nested local callbacks under the originating remote authority. The generic
context/binder also accepts the UI's existing Invocation/Actor representation.
Contexts are single-threaded. Copy a call for a deferred task, re-enter a scope
on its destination, and recheck current policy before each operation. A decision
is a synchronous admission result, not a perpetual grant or cancellation token.

Revoking a session invalidates even reads; `revoke_all` withdraws explicit and
development grants while preserving operations allowed by the remaining cap.
Changing an Access instance does not make an old session valid for the new one.
`Session::attribute` and `created_for_development` are attribution/metadata only;
they do not bypass a live decision. Native hosts decide when to issue a new
session; do not automatically replace a revoked session on a failed request.

Minimal Agent SOP for a host:

1. Bind its own Access, profile and developer/user limits; create the trusted
   session outside payload parsing. Discover domain operations from their owner.
2. Bind and check the same call before query, mutation and visual output. Handle
   `run`/`ask`/`refuse` explicitly; supply structured domain errors and revisions.
3. Keep domain undo/cancel/dry-run semantics in that domain. Carry authority to
   workers, recheck at execution, and stop a refused continuation.
4. Capture actual renderer output with time/version/region provenance. The
   [independent hanim CLI](../../../tools/ci/hanim-play-consumer) exercises real
   playback, 61-frame forward/reverse review and refusal without a display server.
5. Revoke sessions on host disconnect/explicit revocation, observe `agent` logs
   and `control` trace spans. Logging callbacks run outside the policy mutex.

Tests: `mcpp test -p mlibs.control --locked`. UI compatibility checks live in
`openmui.core`, `openmui.agent` and Inspector. This library does not yet provide
a universal provider registry, resource filters, expiry, persistence, async task
cancellation, transport, IR editing or AI decisions. Domain APIs and existing
openas adapters remain responsible for those capabilities.

`OperationScope(context, host_access, call, {name, effect, person})` is the
shared cold admission boundary for native domain adapters. It owns the bound
Invocation and enters the context before deciding, then retains the scope for
its lexical lifetime. `decision()` preserves run/ask/refuse; `execute()` is true
only for admitted non-preview calls. Keep the scope alive through dependent
rendering/output callbacks. Missing host Access refuses remote calls even if
the request carries an Access pointer; empty operation names are refused.
It neither executes domain work nor installs an endpoint or session.

Nested previews propagate for both local and remote calls. The generic binder
keeps compatibility with authority records that have no `dry_run` field;
preview propagation applies when that field exists. Nested local callbacks
still inherit a remote parent's actor/session, while the provider checks the
operation currently entered. Queries obey the same permission boundary as
writes. Scope lifetime is synchronous, not a perpetual grant or asynchronous
cancellation promise. The optional `hanim.control` adapter and its independent
capture consumer now use this implementation.
