# omui — skill

How an Agent (or a person at a terminal) uses a running omui application without a GUI: reads it, finds what to act on, acts, waits, and checks the result in its tree and its pixels. The operations, their parameters, effects, costs and how each is undone are the catalog's: [`omui.ops.omui.toml`](omui.ops.omui.toml), listed for CLI and MCP in [`src/catalog/omui.ops.json`](src/catalog/omui.ops.json) and for reading in [`src/catalog/omui.ops.md`](src/catalog/omui.ops.md). On the wire each is one JSON line, `{"op": "snapshot", …}` (the name without `ui.`).

## Before

1. `describe` (a read): the protocol's version, the application, its operations, the session's policy (which operations are allowed and why not) and the application's verbs with their effects.
2. Know your grant. Every request is admitted through `mlibs.control` under openas: development sessions are pre-authorized, published ones answer by the developer's and the person's settings. `act` and `invoke` run under the effect the action or the verb declares — an irreversible one needs the person's answer. A refusal is an answer, not something to retry around.

## Doing

1. **Read** with `snapshot` (the tree, or a part: `root`, `depth`, `interactive`), then `changes` for what moved since.
2. **Find** by meaning, not by position: `find` with a query (`role`, `name`, `id`, state; wildcards). Interactive nodes have explicit IDs that do not depend on their text — prefer them.
3. **Act** on a node's action (`act`: `activate`, `toggle`, `set_value`, …) or call a verb (`invoke`); `dry_run` says what would happen first. Several steps that belong together go in one `transaction`.
4. **Wait** for the outcome rather than sleeping: `wait` for a node present, absent, or the tree settled.
5. **Input** (`input`: pointer, keys, text a frame at a time) only when the task is about input itself — where a press lands, how a drag feels; the answer says what each press hit. Acting on nodes is the default.
6. **Undo** with `undo` / `redo` and read `history`.

## Verifying

- The tree after: `snapshot` or `find` shows the state the task asked for (values, checked, selected, present).
- The pixels after: `capture` (the window, a node or a region) — the same renderer as on screen. Look at the parts the task touched. With `format` `svg` the same picture comes as vectors: each node's drawing a group marked `data-node` with its path, so you can tell which node drew what and read its exact geometry and colours; `islands` lists what is still pixels (effects) and where.
- Motion: drive the `clock` (`pause`, `step`, `seek`) and capture several moments of a change, not only its ends.
- Performance and health when it matters: `stats` (frame times), `profile` (a trace), `logs` (warnings and errors).
- Record what you did: the requests, their envelopes, the captures' hashes.

## Not here

An application's own verbs and its nodes' actions are its contracts' — `describe` and `snapshot` list them. Development controls (`inspect`, `debug`, `pipeline`, `clock`, `profile`) change only how the application is shown and measured, never its data.
