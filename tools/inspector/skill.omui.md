# Inspector — skill

How an Agent works the Inspector's cellular field (the Reaction lab) without a GUI: reads it, edits its rules or cells, steps its time, looks at intermediate frames and saves it. The operations, their parameters, effects, costs and how each is undone are the catalog's: [`inspector.ops.omui.toml`](inspector.ops.omui.toml), listed for CLI and MCP in [`src/catalog/inspector.ops.json`](src/catalog/inspector.ops.json) and for reading in [`src/catalog/inspector.ops.md`](src/catalog/inspector.ops.md). They are the application's verbs: `{"op": "invoke", "verb": "cellular.paint", "args": {…}}` over the agent endpoint (`--cellular --headless --serve`), or a batch file (`--cellular-batch`, see [docs/cellular.md](docs/cellular.md)). Reading the Inspector's other side — another application's tree — is the UI's operations ([omui's skill](../../modules/projects/omui/agent/skill.omui.md)).

## Before

1. `cellular.describe` (a read): the project format, the limits, the operations and the SOP.
2. `cellular.snapshot`: the revision, the tick, the replay range, the brushes and the state hash. Pass that `revision` with every change: a stale one is refused, so two agents cannot overwrite each other unseen.

## Doing

1. **Load or edit**: `cellular.load` a `*.cellular.omui.json` project, or `cellular.configure` its rules, palette, brushes, rate or boundary over the current field. Both validate before anything is replaced.
2. **Paint** with a named brush (`cellular.paint` x, y, radius 0..8, brush): an input recorded at the current tick, not a tick.
3. **Step** with `cellular.advance` (1..120 ticks); `cellular.play` only for live playback with a host attached.
4. **Look between**: `cellular.inspect` a cell's channels, the host's `capture` for the pixels — every tick when a reaction's boundary matters.
5. **Go back**: `cellular.seek` to a retained frame (exact, inputs included); `ui.undo` takes back a load, configure, paint or reset.
6. **Save**: `cellular.project` exports the field as a reloadable project.

## Verifying

- Load the exported project in a fresh session and advance the same ticks: the same `state_hash`.
- The frames you captured show the change you meant, not only its end.
- Record the operations, their revisions and receipts, and the hashes of the frames.

## Not here

The rules run in `xengine.cellular`; this workspace is not another simulator. GPU-resident fields, sparse worlds and partial refresh are the field-presentation work (plan §6.10).

## The motion workbench (`motion.*`)

1. `motion.open` {file} — a hanim animation document; its problems refuse it.
2. When the person points at a moment (and an object) — "too slow here", "this bounce is too high" — `motion.at` {time, object}: the steps that govern it, innermost first; `motion.knobs` {where} the innermost one's numbers, words and switches, each with its JSON pointer.
3. `motion.set` {where, value} one value; a value the document does not take is refused with its problems and nothing changes. Show the person the same moment after (`motion.seek` and the host's `capture`); `motion.undo` takes it back.
4. A matter of taste (how high, how long) is the person's: offer a few values (`hanim vary`), and set the one they choose.
5. `motion.save` writes the file over: it asks the person first.

