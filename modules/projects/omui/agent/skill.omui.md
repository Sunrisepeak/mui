# omui — skill

How an Agent (or a person at a terminal) uses a running omui application without a GUI: reads it, finds what to act on, acts, waits, and checks the result in its tree and its pixels. The operations, their parameters, effects, costs and how each is undone are the catalog's: [`omui.ops.omui.ts`](omui.ops.omui.ts), listed for CLI and MCP in [`src/catalog/omui.ops.json`](src/catalog/omui.ops.json) and for reading in [`src/catalog/omui.ops.md`](src/catalog/omui.ops.md). On the wire each is one JSON line, `{"op": "snapshot", …}` (the name without `ui.`).

## Before

1. `describe` (a read): the protocol's version, the application, its operations, the session's policy (which operations are allowed and why not) and the application's verbs with their effects.
2. Know your grant (D-155). In **development mode** (`omui dev`) you have every control a developer's own tools have: nothing is refused for want of the person's answer, and whether to ask them first is your judgement and their instructions to you — ask before what they would want to decide, not by rule. In a **release** the session's policy decides (`describe` shows it): an irreversible or external action may need the person's answer, and a refusal is an answer, not something to retry around. The person sets either level (`omui.config.ts`, `--agent-mode`); every request is still admitted through `mlibs.control` under openas.

## Doing

1. **Read** with `snapshot` (the tree, or a part: `root`, `depth`, `interactive`), then `changes` for what moved since.
2. **Find** by meaning, not by position: `find` with a query (`role`, `name`, `id`, state; wildcards). Interactive nodes have explicit IDs that do not depend on their text — prefer them.
3. **Act** on a node's action (`act`: `activate`, `toggle`, `set_value`, …) or call a verb (`invoke`); `dry_run` says what would happen first. Several steps that belong together go in one `transaction`. Before steps the person must decide on, `preview` them: what they would make, as pictures before and after and as the semantic changes, run unseen and taken back — nothing stays, no journal entry, no history; a step that asks the person is shown as such and not done. Show the person the pictures, then act on their word.
4. **Wait** for the outcome rather than sleeping: `wait` for a node present, absent, or the tree settled.
5. **Input** (`input`: pointer, keys, text a frame at a time) only when the task is about input itself — where a press lands, how a drag feels; the answer says what each press hit. Acting on nodes is the default.
6. **Undo** with `undo` / `redo` and read `history`.
7. **Say what you are about to do** before a run of steps the person watches (`intend`: the steps or a note, the node it is about), and **hand back** when you stop with something left (`intend` with `hands_back`: what you did, what is left, what you were unsure of) — their screen shows it. If an action comes back `conflict` without `retry_after`, the person has taken over: stop, say so, and wait for them to hand back; with `retry_after`, they are operating that node — try it then, not before.

## Verifying

- The tree after: `snapshot` or `find` shows the state the task asked for (values, checked, selected, present).
- The pixels after: `capture` (the window, a node or a region) — the same renderer as on screen. Look at the parts the task touched. With `format` `svg` the same picture comes as vectors: each node's drawing a group marked `data-node` with its path, so you can tell which node drew what and read its exact geometry and colours; `islands` lists what is still pixels (effects) and where.
- Motion: drive the `clock` (`pause`, `step`, `seek`) and capture several moments of a change, not only its ends.
- Performance and health when it matters: `stats` (frame times), `profile` (a trace), `logs` (warnings and errors).
- Record what you did: the requests, their envelopes, the captures' hashes.

## Designing live (development mode, `omui dev`)

The application is its own designer (D-149); its `design.*` verbs (`ui_invoke`) are the loop:
1. `design.sources` — the TypeScript each script and module runs now.
   Before writing, read what the application offers to write with: `design.api` (each language's API over its components), or `describe` with `api: true` (every builder, native module, theme token and verb, as data — `omui describe --api` and `.omui/types/` give the same from the command line; `omui check` type-checks what you wrote against it).
2. `design.edit` {path, text} — new text runs at once, state kept; the answer is the semantic changes and the time. A text that does not run is refused with its line, nothing changed. The person sees each edit written in, and your note beside it.
3. `design.expect` {say, query} — what the person wants, as a query that holds or not; `design.check` reads every expectation against the interface as it is (facts, with what the last press on a node came to).
4. `design.handover` — the person tries it; when they say they are done, `design.check`, then `design.save_scenario` {name}: their steps and the expectations as a test; `design.replay` {name} plays it again.
5. `design.commit` writes the edits to the files (the TypeScript is the one source), `design.discard` drops them.

When you (or the person) edit the files themselves rather than through `design.edit`, `reload` reloads what was saved and answers what it did — the semantic changes, where the pixels changed (that region before and after), the motion it started sampled over time and whether it settled — so you confirm the change without comparing screenshots; `reload` with `last` says what the last automatic reload did.

## When the person says something did not work

"I clicked and nothing happened" is a report to check, not a fact to repeat. Ask the recorder (D-148) what their input did: `explain` with `at` (where they clicked) gives that press from press to release — what it hit, what stopped it (a disabled control, a release outside it, nothing there to press, outside a modal, refused) and the actions it led to, in one sentence and the records. `timeline` lists the recent input events and what came of each (`since` a cause, `limit`). Then say what you found, fix the cause, and check it the same way. Development mode records everything; elsewhere the recorder keeps what did not happen (errors) unless the application says otherwise, and what a person types is counted, never kept.

## Not here

An application's own verbs and its nodes' actions are its contracts' — `describe` and `snapshot` list them. Development controls (`inspect`, `debug`, `pipeline`, `clock`, `profile`) change only how the application is shown and measured, never its data.
