# hanim.studio — skill

How an Agent (or a person at a terminal) makes an animation without a GUI: writes a document, checks it, reads its numbers at the moments that matter, looks at its frames. The operations are the catalog's: [`studio.ops.omui.toml`](studio.ops.omui.toml), listed in [`media/ops/studio.ops.json`](media/ops/studio.ops.json) and [`media/ops/studio.ops.md`](media/ops/studio.ops.md); each is the `hanim` command (`document.check` is `hanim check FILE`): one JSON answer, exit 0 done, 1 the document's problems, 2 a wrong command. The document format is in [README.md](README.md).

## Before

1. Start from an [example](examples/README.md) close to the task: twenty, each composed from hanim's recipe library.
2. Name every object with a stable id; give each a shape (`rect`, `circle`, `ellipse`, `polygon`, `star`, or SVG `path` data), a fill or a stroke, where it stands.

## Doing

1. Write the program as steps: the recipes (entrances, exits, cues, idle loops, moves) for the common motions, `move_to` / `scale_to` / `morph` / … with an ease or a spring for the rest, composed in `sequence`, `parallel`, `stagger`, `loop`. Put a `marker` at each moment the task names.
2. `document.check`: fix every problem at its JSON pointer, then check again. A composition hanim refuses (two recipes with effects on one object at once) is said there too.
3. `document.sample` at the markers and between them: positions, scales, rotations, fills, opacity, bounds — the numbers to hold against the task.
4. `document.render --frames 8`: look at the strip. The whole course, not only its ends: entrances appear only when they begin; later objects draw over earlier ones in the document's order. With `--svg` each frame is written as SVG too (`hanim render FILE --out PREFIX --svg`): vectors at any size, to read exact positions, paths and colours rather than estimate them from pixels; a frame's `islands` counts what had to stay pixels (none in the examples).

5. When the person points at a moment (and an object) — "here it is too slow", "this bounce is too high" — `document.at` (`hanim at FILE --at T --object ID`) names the steps that govern it, innermost first: change the innermost one's numbers at its JSON pointer, not a guess at which step it is; check, sample and render again, and show them the same moment before and after.

## Verifying

- The task's numbers hold in the samples (where things end, how high, when, how many times).
- Every object stays where the task allows (inside the stage: compare bounds at many moments).
- The strip shows the motion asked for; record the document, its samples and its strip's frame hashes (the render report).

## Not here

Operating a running host's Engine is hanim's own catalog ([hanim's skill](../skill.omui.md)); authoring in code is hanim's C++ API.
