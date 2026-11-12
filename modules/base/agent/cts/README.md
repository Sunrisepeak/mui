# mlibs.agent.cts

The agent contract's conformance suite (D-174): the eight native properties as cases, run on any subject of [mlibs.agent](..) through the same session an agent has — an interface, a game, an animation stage, a field. On [mlibs.cts](../../cts): every case passed, failed or unsupported.

The suite knows no subject's operations. A subject's owner hands it a **probe**: how one is made (the same first state each time), and which of its requests show each property.

```cpp
import mlibs.agent.cts;
namespace contract = mlibs::agent::cts;

const contract::Probe probe{
    .make = [] { return std::make_unique<Game>(first_state()); },
    .read = R"({"op":"world.observe"})",
    .change = R"({"op":"world.step","steps":3})",
    .preview = R"({"op":"world.branch","steps":30})",
    .script = {R"({"op":"world.act","axis":[1]})", R"({"op":"world.step","steps":40})"},
};
contract::declare(probe, {.subject = "xengine.runtime"});   // mtest's cases: xengine.runtime/agent.read.pure, …
expect(contract::unsupported(game.capabilities()) == std::vector<std::string>{"agent.source.chain"});   // what it lacks, pinned
```

| # | Property | Case | Needs | Checks |
|---|---|---|---|---|
| 1 | One source | `agent.catalog.complete` | — | What it can do is its catalog: every operation addressed under the subject, with what it does, its effect and (when it can be taken back) how; `describe` gives the same, briefly and whole; the listing has each |
| 1 | | `agent.catalog.stable` | — | Made twice: the same state hash, the same answers byte for byte; asked again, the same |
| 2 | Transactional | `agent.preview.untouched` | preview | After a preview the state hash is the one before |
| 2 | | `agent.undo.returns` | control, and history or time | A change changes the hash; taken back — undone, or sought to before it — it is the one before |
| 3 | Deterministic | `agent.time.replays` | time | The same requests on two: the same answers and the same hash after each |
| 4 | Output traced to its source | `agent.source.chain` | provenance | From something it put out, a chain that is not empty |
| 5 | The actor in the model | `agent.actor.recorded` | control, causes | The record of a change names the session's actor |
| 6 | Authority at the core | `agent.authority.first` | — | A request the policy refuses is answered `refused`, with no data, and did not run (the hash); allowed, the same request runs |
| 7 | Observation made for an agent | `agent.read.pure` | observation | A reading is declared a read, leaves the hash, and repeats |
| 7 | | `agent.observe.budget` | observation, budget | Asked within a budget, the answer is within it |
| 8 | The working details | `agent.errors.structured` | — | An unknown operation is `not-found`, a line that is no request `invalid-argument`; each says what is wrong, in records that echo `re` and end in a result; the state untouched |
| 8 | | `agent.causes.explained` | control, causes | A change on record is explained by a chain that is not empty; explaining changes nothing |

- **Unsupported comes from the subject alone**: a case whose needs its `capabilities()` lack does not run, and is never counted as passed. A runner may require a capability (`Policy::required`): then its absence fails.
- **A claim not shown fails**: a subject that says it has a capability whose probe names no request for it fails that capability's cases.
- **Each subject runs it in its own tests** — the suite is here with the contract's owner; a package that is a subject registers its probe (lint `agent-surface`).

Its own package so that the kernel carries no test framework.

```sh
mcpp test -p mlibs.agent.cts
```
