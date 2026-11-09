# xengine — skill

How an Agent (or a person at a terminal) works with xengine's shared editing pieces without a GUI — now the timeline every editor shares (a video's, a sequencer's, an animation editor's). The operations are the catalog's: [`xengine.ops.omui.ts`](xengine.ops.omui.ts), listed in [`media/ops/xengine.ops.json`](media/ops/xengine.ops.json) and [`media/ops/xengine.ops.md`](media/ops/xengine.ops.md). Each is `xengine timeline <command> FILE …`: one JSON answer, exit 0 done, 1 refused or failed with why, 2 a wrong command.

## Before

1. `timeline.check`: the tracks and clips, the markers, the length — or every problem at its line and column with the nearest right word. Fix a file until it checks clean before editing it.
2. Times are exact: `90f` (frames at the timeline's rate), `1.5s`, `250ms`, `123fl` (flicks). A frame is never a rounding.

## Doing

1. Ask before changing: `timeline.at` a moment says what plays there; `timeline.edit … --dry-run` says what an edit would leave.
2. Edit: `split` a clip at a moment, `trim` it (new start and length, its source staying under the same time), `move` it (on its track or another of its kind), `remove` or `ripple-remove` it (later clips moved back by its length). An edit that cannot be made is refused whole: the file stays as it was.
3. Take an edit back with `timeline.undo`: the file's text before it, exactly.

## Verifying

- `timeline.check` is clean after every edit, and `timeline.at` the moments the task names says what the task asked.
- An undo gives back the very bytes the file had (compare a hash before and after).
- Record the edits and their answers.

## Not here

Playing a timeline (pictures, sound) is its players' — the promo video's renderer, the mixer. A game's agent player has its own catalog (breakout's); the cellular workspace is the Inspector's.
