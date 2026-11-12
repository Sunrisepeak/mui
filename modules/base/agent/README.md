# mlibs.agent

The agent kernel (D-173): what [openas](https://github.com/openlifa/openas)'s model needs inside a process, for any subject — an interface, a world, an animation stage, a renderer's frame. It is below every engine, so a simulation, a renderer or a game with no interface is found, observed and operated the way an application with one is. It knows neither the interface nor a game; authority (policy, sessions, `decide`) is [mlibs.control](../control)'s.

Six things every engine gives an agent from its data model on — state, observation, control, time, causes, identity and authority — reach the agent through here. What is here today:

| Part | |
|---|---|
| `:endpoint` | Where agents find a program and talk to it: a local connection named after the program (a Unix socket, a named pipe), a descriptor beside it (`<name>.endpoint.omui.json`: its application, the interface its sessions speak), optionally this machine's loopback behind a secret (a device's endpoint, forwarded). Lines in, lines out; one `Session` a connection, made by the owner; served by `pump()` at the owner's safe point — a frame's boundary, a step's |
| `:envelope` | A request and its answer as records (openas §6): one JSON object a line with its `op`, a `re` echoed in every record, a person's `answer`; `data`, `notice`, `error` records and last a `result` with the exit code. The framing is the same for every subject |
| `:subject` | What an agent finds, observes and operates: a `Subject` says its name, the interface its operations speak, what it can do of the six (and what it cannot), its operations with their effects, its state's hash. `SubjectSession` is a connection's conversation with a process's subjects — `subjects`, `<subject>.describe`, `<subject>.<operation>` — and decides every operation first (`mlibs.control`): what a person must agree to ends in `needs-answer` until the request carries the answer, what is refused does not run |

Next, each when its second consumer is there (the plan's §3.2): the catalog from `*.ops.omui.ts` with its tiers of disclosure, observation with a budget, previews and transactions, the record of causes, real-time sessions, presence and claims, history, background tasks.

```cpp
import mlibs.agent;
namespace agent = mlibs::agent;

class WorldSession final : public agent::Session {
    void request(std::string_view line, const agent::Reply& reply) override { reply(answer(world_, line)); }
};

agent::Endpoint endpoint({.name = "sandbox", .app = "org.example.sandbox", .interface_name = "example.world@1",
                          .session = [&] { return std::make_unique<WorldSession>(world); }});
endpoint.start();
while (running) {
    world.step();        // the owner's work
    endpoint.pump();     // requests, at a step's boundary: the state may be touched here
}
```

- **No request reaches the owner's state from another thread.** Connections are read on background threads; `pump` serves what arrived, on the thread that calls it. A request never enters a loop over cells, pixels or samples.
- **A session is a connection's conversation**: its `request` answers by lines (now or later, at a safe point), its `poll` runs once a pump (a condition waited for, a person's answer).
- **The lines are the session's.** `SubjectSession` speaks the envelope above for any subjects; omui's interface speaks `openmui.tree@0.1` through a session of its own (`omui.agent`), unchanged.
- **Nothing reaches a subject undecided.** With an `Access`, each operation is decided by its declared effect, the actor and the session before `operate` is called; a refusal is an answer, never a silent skip.

```cpp
class World final : public agent::Subject { /* name, interface, capabilities, operations, state_hash, operate */ };
World world;
agent::Endpoint endpoint({.name = "sandbox", .session = [&] { return std::make_unique<agent::SubjectSession>(std::vector<agent::Subject*>{&world}, options); }});
// → {"op":"subjects"}   → {"op":"world.describe"}   → {"op":"world.observe","re":1}
```

Its own package rather than part of `mlibs.control`: control takes openas, logging and tracing alone and has no transport; this takes `mcpplibs.os`'s connections and `mlibs.json`, and control for its decisions.

```sh
mcpp test -p mlibs.agent
```
