# hanim — skill

How an Agent (or a person at a terminal) does animation work with hanim without a GUI. The operations, their parameters, effects, costs and how each is undone are the catalog's: [`hanim.ops.omui.ts`](hanim.ops.omui.ts), listed for CLI and MCP in [`src/control/hanim.ops.json`](src/control/hanim.ops.json) and for reading in [`src/control/hanim.ops.md`](src/control/hanim.ops.md).

## Before

1. Read the catalog listing: thirteen `animation.*` operations; `read` ones change nothing, `reversible` ones say their undo, `irreversible` ones (`animation.cancel`, `animation.release`) cannot be taken back.
2. Know your grant: a host admits each call through `mlibs.control` (development sessions are pre-authorized, release profiles ask the person); a refusal is an answer, not an error to retry around.

## Doing

1. **Prepare first** (`animation.prepare`, a read): the plan's tracks, segments, channels and duration come back before anything moves. Check the duration and the channels you meant.
2. **Play** (`animation.play`): you get a playback. Note its id; everything after names it.
3. **Look at the whole course, not its ends**: `animation.seek` (or `animation.seek-progress`) to the start, several middles and the end, reading `animation.inspect` and drawing each with the shared renderer (hanim.render.vector into xrender's CPU raster) — a motion is judged by its intermediate frames. Seeking is exact and repeatable: the same time, the same frame.
4. **Steer** with `animation.pause` / `resume` / `reverse` / `rate`; each says its undo.
5. **Finish**: `animation.cancel` (with `restore` to put the objects back) or `animation.release` when the playback is no longer needed. Both are irreversible.

## Verifying

- The plan read back (`animation.plan`) is the plan you prepared: tracks, channels, duration.
- Frames sampled at the same time twice are bit-identical; out-of-order seeks give the same frames.
- At the end the objects are where the program says (`animation.inspect`, and a drawn frame).
- Record what you did: the operations, their arguments and receipts, the frames' hashes.

## Authoring without code: a document and the `hanim` command

The command's operations are [hanim.studio's catalog](studio/studio.ops.omui.ts) (`document.check`, `document.sample`, `document.render`; listed in [`studio/media/ops`](studio/media/ops)). Twenty documents to start from are [hanim.studio's examples](studio/examples/README.md).

1. Write `<name>.animation.omui.json` ([hanim.studio](studio/README.md)): the stage, the objects (shapes, fills, where they stand), and the program as steps (`move_to`, `drop`, `morph`, `scale_to` with a `spring` …) in sequences and parallels. Add `marker`s at the moments you will check.
2. `hanim check FILE`: the plan (duration, tracks, markers) or every problem at its JSON pointer — fix them all, then check again.
3. `hanim sample FILE --at T …`: each object's position, scale, rotation, fill and bounds at those moments — the numbers to verify against what was asked (where it lands, that it overshoots, what shape it ends as).
4. `hanim render FILE --out PREFIX --frames N`: the frames, a strip of them and a report with each frame's pixel hash. Look at the strip: the whole course, not only its ends.

## Not here

Authoring in code (writing a Program) is hanim's C++/script API; documents cover shapes and the common steps. Rendering is xrender's. Permission is the person's (mlibs.control and openas).
