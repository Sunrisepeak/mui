# hanim.control

Optional native control of the same public Engine, selected by the existing
product's `control` feature. It implies `engine` and adds `mlibs.control` /
openas; it adds no UI, renderer, network, model or alternate player. Default
Hanim and renderer-only consumers do not acquire this dependency.

```toml
[dependencies]
hanim = { path = ".../modules/gfx/hanim", features = ["control"] }
```

```cpp
import std;
import hanim; // exports hanim.control when selected

auto made = hanim::Engine::make();
if (!made) return;
auto engine = std::move(*made);
auto access = std::make_shared<mlibs::control::Access>(
    mlibs::control::policy_for_profile("dev"));
mlibs::control::InvocationContext context;
hanim::control::Controller controller(engine, access, context);
// Only the trusted host creates sessions and marks incoming requests remote.
mlibs::control::Invocation request{
    .actor={openas::Kind::agent, "animator", {}}, .remote=true,
    .access=access, .session=access->open_session()};
auto available = hanim::control::operations();
auto current = controller.list(request);
// Check current, current->executed and current->value before reading the list.
```

The Controller borrows Engine and InvocationContext; both must outlive it.
Calls are synchronous and serial, like Engine. The context should be shared
with dependent renderer/tool operations, never concurrently shared by workers.
Workers copy Invocation metadata, enter their own destination scope and recheck
live permission at execution. Controller owns no event loop or worker thread.

The fixed catalog exposes stable domain names, openas effects and purposes.
Typed C++ parameters/results are declared in `control.cppm`; a transport adapter
must validate its own payloads and authenticate/bind actor, session and remote
metadata. Invocation is not itself an authentication token. These names are
not a new wire protocol, JSON schema or automatically installed RPC endpoint.
Public static discovery itself exposes no instance resources and grants nothing.

| Operation | Domain contract | Effect |
|---|---|---|
| `animation.prepare` | Program and ProgramLimits → immutable PreparedProgram | read |
| `animation.play` | Program + limits or PreparedProgram → PlaybackRef | reversible |
| `animation.list` | owned PlaybackInfo values | read |
| `animation.inspect` | PlaybackRef → PlaybackInfo | read |
| `animation.plan` | PlaybackRef → owning immutable plan lease | read |
| `animation.seek` | PlaybackRef and absolute `seconds` | reversible |
| `animation.seek-progress` | PlaybackRef and normalized double | reversible |
| `animation.pause`, `animation.resume`, `animation.reverse` | native transport | reversible |
| `animation.rate` | nonzero signed double | reversible |
| `animation.cancel` | PlaybackRef and CancelMode; terminal cancellation | irreversible |
| `animation.release` | invalidate the playback handle | irreversible |

`Reply<T>` is an expected receipt or Failure. An executed query owns its result;
no mutable Engine frame spans escape this adapter. An admitted dry run returns
`executed=false` with no value, including read requests. This is admission
preview only: it does not validate/compile the Program or fabricate a domain
result. `Failure::admission` preserves `ask` versus `refuse`; only `run` reaches
Engine. The host handles approvals through existing policy/grant mechanisms;
an answer string does not create a grant. `Failure::engine` preserves the full
EngineFailure after admission. Permission checks occur before handle queries,
so rejected requests do not probe playback state.

Openas reversibility classification is not an automatic undo journal. The host
retains any prior state it needs for undo. Cancellation/release are classified
irreversible because their old handles cannot be resumed/restored by Controller.
Normal Engine errors, including occupied objects and stale handles, remain in
force; control does not bypass ownership to make a command succeed.

Authorization belongs to `mlibs.control::OperationScope`, not a duplicated
Hanim policy evaluator. It binds the originating actor/session and operation
before authorization/domain callbacks; nested calls retain that authority and
preview state. Current entered operations are checked separately from the
initiating operation retained in Invocation. Development grants remain subject
to developer/user/session restrictions; revoked/foreign sessions are refused.
Local native calls are trusted host calls, not untrusted JSON claiming locality.
In-process Agent work that is subject to user/session limits must also carry
delegated authority; transport locality alone must not establish trust.
Use separate Access instances or host resource filters where finer instance /
object partitions are required; Controller adds no per-object ACL of its own.

Contexts do not inherit across unrelated instances or different Invocation
representations. A UI compatibility adapter must explicitly carry the shared
Access/session, actor, origin and preview flags into a native Invocation; an
empty local call is not a substitute for that bridge. This module does not
install that UI bridge automatically.

Minimal headless SOP:

1. Create Engine resources and bind the trusted Access/profile/session/context.
   Discover operations and select a public Program, including feedback recipes.
2. Call `prepare` / `play`, inspect typed admission and domain failures, then
   use `inspect` / `plan` to identify position, revisions, channels and limits.
3. Seek several intermediate times and interruption boundaries. A renderer host
   uses the same InvocationContext and an `OperationScope` for its capture/read
   operation, keeping authorization active through rendering and result output.
   Controller itself does not create pixels, write files or select a renderer.
4. Verify actual pixels plus semantics, replay backwards, and exercise live
   policy restriction/revocation. Re-enter copied worker calls under current
   policy. Do not renew a revoked session automatically.
5. Cancel/release explicitly and destroy Controller before its Engine/context.
   Observe `hanim.control` trace spans and refusal/domain-error logs.

The independent `tools/ci/hanim-play-consumer` CLI uses Controller plus the
shared capture scope for real 61-frame Agent reviews. The feature consumer
checks the selected closure and executes this optional path on native and Web.
Object editing, generic provider registration, transport, resource ACLs,
automatic history/undo and asynchronous cancellation remain separate work.

Prepared playback shares `animation.play` admission with authored playback and
retains the exact plan lease; it does not compile again. Owning a prepared plan
does not grant permission to play it after a session is restricted or revoked.
