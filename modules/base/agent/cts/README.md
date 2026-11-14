# mlibs.agent.cts

The agent contract's conformance suite (D-174): the eight native properties, and the norms over them that let an agent begin with nothing but the endpoint (D-186), as cases, run on any subject of [mlibs.agent](..) through the same session an agent has — an interface, a game, an animation stage, a field. On [mlibs.cts](../../cts): every case passed, failed or unsupported.

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
| 9 | It guides itself (D-186) | `agent.guide.runs` | — | It says what it is and has tasks to begin with (its catalog's `doc` and `tasks`), and `describe` gives them; every task, on a subject as it is made, runs step by step as written; an operation that needs arguments shows a request of it, and no example is answered `invalid-argument`; what follows an operation (`then`) is a request of the catalog, and those the probe's change is answered with run as given |
| 10 | Every error carries a way out (D-186) | `agent.errors.next` | — | A line that is no request, a subject that is not here, each operation's name a letter short, each operation sent without what it needs: the error carries `next` — each way with why and a request; a session's own request (`describe`, `subjects`) runs as given, any other is a request of the catalog; a name a letter short is led to the name; the state untouched |
| 11 | What was asked is a fact (D-186) | `agent.log.fact` | — | A request that failed and (with control) a change are each answered with a number (`fact`, with the result) and are on record under it (`log`): what was asked, how it ended, who asked — the session's actor; a reading that answers is no fact; asked for what failed, or what came after a number, the record answers that; asked again, the same; the state untouched; of a subject that explains its changes, the change's fact says where the subject's own record has it (`at`), and what `explain` answers has the same place |
| 12 | Found by intent (D-186) | `agent.find.own` | — | `find` with an operation's own summary has the operation among the first three, and a task's summary the task; each thing found has a line and requests; what its content names (`terms()`, the first eight) is found by name and leads to a request of the catalog; words nothing has are answered with nothing found and where to look, each of which runs; `find` without `q` is `invalid-argument` with a way out; the state untouched |

- **A task runs on the probe's subject**: a catalog's tasks are its interface's — what any subject speaking it can be asked — so they name no node, handle or place only one application has; a step takes such a value from the answer before it (`{"$last": pointer}`).
- **Unsupported comes from the subject alone**: a case whose needs its `capabilities()` lack does not run, and is never counted as passed. A runner may require a capability (`Policy::required`): then its absence fails.
- **A claim not shown fails**: a subject that says it has a capability whose probe names no request for it fails that capability's cases.
- **Each subject runs it in its own tests** — the suite is here with the contract's owner; a package that is a subject registers its probe (lint `agent-surface`).

Its own package so that the kernel carries no test framework.

```sh
mcpp test -p mlibs.agent.cts
```
