# The agent contract on one page (`mlibs.agent.contract@1`)

What any subject — an interface, a world, an animation stage, a frame — gives an agent, and what an agent can count on, whatever engine is behind it. The parts are explained in the [README](README.md); the cases that hold a subject to this are [mlibs.agent.cts](cts/README.md). Records, exit codes, error codes and decisions are [openas](https://github.com/openlifa/openas)'s; this page says how the kernel uses them. `tests/contract_test.cpp` in [cts](cts) holds every list here to the code.

## 1. Where

A program is found at a local connection named after it (a Unix socket, a named pipe), with a descriptor beside it (`<name>.endpoint.omui.json`: its application and the interface its sessions speak). One JSON object a line in, records a line out. Requests are served at the owner's safe point (a frame's or a step's boundary), never from another thread.

## 2. A request and its answer (openas §6)

A request is `{"op": …, "re": …, …}`; `re` is echoed in every record of its answer; `answer` carries a person's answer when one was asked for. An argument `{"$last": "/json/pointer"}` is that place in the data the conversation's latest answer gave.

Records, in this order of kinds: `phase`, `progress`, `log`, `notice`, `error`, `data`, and last `result` with its exit code:

| exit code | |
|---|---|
| 0 | done |
| 1 | failed |
| 2 | needs an answer: the same request again, with `answer` |
| 3 | not ready: the same request again, later |

An `error` has a `code` — `needs-answer`, `refused`, `not-found`, `invalid-argument`, `stale`, `timeout`, `failed`, `internal` (one a reader does not know is `failed`) — a text, and `next`: ways out, each `{"why", "request"}` a request to send as it is; an error that is a domain's (`mlibs.error`) also says its code's way out in words, `way`. A `result` of a change carries the number of its fact (`"fact": 7`) and, from the catalog, `then`: the reading that shows what it did.

## 3. The session's own requests

| request | answers |
|---|---|
| `{"op":"subjects"}` (or `help`) | who is here — each subject's name, interface, capabilities and state hash — and the session's own requests below |
| `{"op":"describe","subject":…}` | what the subject is, the tasks to begin with (each step a request to send as it is), its operations with a line each; `"operation":…` one whole, with examples; `"detail":"full"` the catalog's listing |
| `{"op":"find","q":…}` | operations, tasks and the subjects' content that have the words, the best first, each with requests to send; whole numbers, the same answer on every machine; nothing found says where to look |
| `{"op":"log"}` | what was asked, as facts: every change and every failed request — who, of which subject, what, how it ended; `since`, `subject`, `failed`, `limit` |

Anything else is a subject's operation by its whole name (`world.observe`), decided before it runs: what a person must agree to ends in `needs-answer`, what the policy refuses is `refused` and does not run. The actor is the session's, never what the line claims.

## 4. A subject

A name, the interface its operations speak (`xengine.world@0.1`), a catalog (`<product>.ops.omui.ts`: each operation's effect, parameters, cost, how it is undone, examples, `then`), a guide (what it is, its tasks), terms its content names, its state's hash, where its own record of changes stands, and what it can do:

| capability | |
|---|---|
| `observation` | a projection of its state designed for an agent |
| `budget` | … answered within a budget of bytes the request gives |
| `control` | operations that change it |
| `time` | steps, seeks, replays |
| `causes` | why its state is what it is, and who made each change |
| `provenance` | from what it puts out back to what made it |
| `preview` | a branch run ahead and compared, the state untouched |
| `history` | commits, branches, what was undone |

## 5. What is held (the cases)

Twelve properties, each a case or two in [mlibs.agent.cts](cts/README.md): one source (the catalog is complete and stable), transactional (a preview leaves it, an undo returns), deterministic, output traced to its source, the actor recorded, authority first, reading pure (and within a budget), structured errors and causes explained, a guide that runs, every error with a way out, what was asked a fact, found by intent. A case needing a capability the subject does not have is *unsupported*: never a pass.
