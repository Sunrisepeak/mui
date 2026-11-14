# omui.agent

The agent endpoint of an openmui application (spec §9, plan §11):
`openmui.tree@0.1` as JSON lines.

Its operations are its catalog's, [`omui.ops.omui.ts`](omui.ops.omui.ts)
(convergence plan §10.3): `omui-gen ops` makes the protocol's table
(`omui::agent::catalog`, effects and admission from it), the CLI/MCP listing
and the reference in `src/catalog/`; how an Agent works with it is
[`skill.omui.md`](skill.omui.md).

- `Protocol` — one request per line (`describe`, `snapshot`, `changes`,
  `find`, `act`, `invoke`, `undo`, `redo`, `history`, `transaction`, `wait`,
  `capture`), answered by envelope records ending in a
  `result`; `"re"` correlates records with requests; `wait` answers when the
  condition holds (checked after every frame) or times out with exit code 3;
  `act` applies the effect rules (`needs-answer`, exit code 2) and records the
  actor (`agent:planner;via=person:alice`) in the runtime's journal. Errors
  carry hints that show the correct form.
- Undo, transactions, dry runs (S-12, S-13): `undo` / `redo` take back or
  put forward a step of a scope's history (`"scope"`, default the window),
  `history` lists it, `transaction` performs `steps` as one step (the first
  that fails takes the others back); `"dry_run": true` on `act`, `invoke`,
  `undo`, `redo` and `transaction` answers what would happen — `outcome`,
  `effect`, `undoable`, the value before and after — and changes nothing.
- Development operations (D-59): `inspect` (a node's type, source line,
  props, layout and state), `stats` (frame-time mean and percentiles, the
  phases, node counts), `profile` (`start`, then `stop` returns the per-span
  summary and, with `"chrome":true`, the Chrome trace), `logs` (recent
  records by level and category) and `debug` (the runtime's overlays, the
  slow-frame threshold and the log filter, while it runs), and `pipeline`
  (the frame's stages, plugins and times; `replace`, `insert` and `remove`
  a stage between frames — D-66), and `clock` (pause, resume, rate, step,
  seek, release: the application's time; seeking back replays animations).
- `Endpoint` — listens through `mcpplibs.os.ipc` (a local socket) in a
  per-user directory, writes `<name>.endpoint.omui.json` beside it, reads
  connections on background threads and serves requests on the UI thread in
  `pump()`, between frames. Each connection has its own `changes` baseline.
- `Client` and `list()` — what `omui agent` uses.

```text
→ {"op":"find","q":{"role":"button","name":"Delete*"}}
← {"kind":"data","data":{"nodes":[{"id":"notes/delete","role":"button","actions":[{"name":"activate","effect":"irreversible","answer":"yes"}]}]}}
→ {"op":"act","node":"notes/delete","action":"activate","actor":"agent:planner;via=person:alice"}
← {"kind":"error","code":"needs-answer","text":"…","answer":["yes"],"hint":"call again with \"answer\": \"yes\""}
← {"kind":"result","exitCode":2}
→ {"op":"act","node":"prefs/volume","action":"set_value","args":{"value":42},"dry_run":true}
← {"kind":"data","data":{"dry_run":true,"outcome":"run","effect":"reversible","undoable":true,"value":{"before":30,"after":40}}}
← {"kind":"result","exitCode":0}
→ {"op":"transaction","label":"quiet","steps":[{"node":"prefs/pinned","action":"toggle"},{"node":"prefs/volume","action":"set_value","args":{"value":"loud"}}]}
← {"kind":"error","code":"invalid-argument","text":"step 2 (set_value on 'prefs/volume'): …; the 1 step before it was taken back","step":1,"rolled_back":1}
← {"kind":"result","exitCode":1}
```

Depends on `omui.core`, `omui.view`, `omui.semantic`, `openmui.trace` and `mcpplibs.os`.

## Agent surface

The interface is the model the other engines' subjects are written after, and is itself a `Subject` of the agent kernel ([mlibs.agent](../../../base/agent); D-173): `InterfaceSubject`, `ui`. It adds no operation and changes none — `ui.snapshot` is the protocol's `snapshot`, decided by the runtime's own access as ever, answered in the same records. What is new is where it is found: `ProcessSession` — a connection's conversation with an application — speaks `openmui.tree@0.1` as it always did and, on the same lines, the kernel's requests for everything the process holds: `subjects`, `describe` a subject or an operation, any operation by its whole name (`ui.find`, `world.observe`, `frame.at`). The two cannot be taken for each other: the protocol's requests have no dot in their names. What else the process holds is the runtime's (`Runtime::add_subject`; a component through `Services::add_subject`): a game shown in a viewport, the host's frame.

Its operations are [omui.ops.omui.ts](omui.ops.omui.ts); how to use them, [skill.omui.md](skill.omui.md).

| | |
|---|---|
| Subject and addresses | `ui`, interface `openmui.tree@0.1`. A node is its id path (`main/compose/draft`); a place is `[x, y]` in logical pixels |
| State | What was set in the interface — text, switches, values, scroll, focus (`ui.state`; `ui.restore` puts it into another instance); the hash is that, hashed |
| Observation | `ui.snapshot` (by root, depth, interactive only, paged), `ui.changes` since the last, `ui.find` by role, name and state, `ui.capture` (a node or a region, PNG or SVG), `ui.inspect`, `ui.stats`, `ui.logs` |
| Control | `ui.act` (a node's action, with its effect's rules: a dry run, `needs-answer`), `ui.invoke` (an application's verb), `ui.transaction` (steps as one), `ui.input` (as a person's devices give it) |
| Time | The interface's clock is held, stepped and sought by `ui.clock`; a session recorded (`ui.record`) replays in a fresh instance to the same bytes. Not as the suite's replayed requests: see "Not here" |
| Preview | `ui.preview`: steps performed on a branch — what would change, with pictures where a host draws — and nothing done |
| Causes | `ui.timeline`: what each input event did at the recorder's level, and `actions` — every action with who did it, whatever led to it, kept at every level; `ui.explain`: a press from press to release, and a sentence saying what happened |
| Source | `ui.drew`: the node a pixel's colour came from, the nodes it sits in, where each was built — where a host draws (`ProtocolOptions::pixel_owner`); the frame's own chain is the `frame` subject's |
| History | `ui.undo`, `ui.redo`, `ui.history` by scope; Live Design's commits and branches are `design.*` |
| Budgets and tiers | A snapshot is paged by nodes (`limit`, `offset`, `depth`, `children_limit`); an agent player's tiers are the game's (`world`) |
| Not here | **A budget of bytes** on an answer (`capabilities.budget` is false: pages are counted in nodes), and **time as replayed requests** (`capabilities.time` is false: an answer carries the runtime's clock, so the same requests on two instances are the same state, not the same bytes) — the contract suite reports both unsupported. `ui.drew` without a host that draws: unsupported there, and said |

The agent contract's suite ([mlibs.agent.cts](../../../base/agent/cts)) runs on it in `tests/contract_test.cpp` (no host: 9 passed, 3 unsupported) and, where a host draws, in [omui.host.headless](../host/headless)'s tests (a pixel's source too). The suite found what the interface lacked: an agent's own action was in the journal with its actor, and no operation read it — `ui.timeline` answers `actions` since.
