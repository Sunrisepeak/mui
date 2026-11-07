# gongfu — skill

How an Agent (or a person at a terminal) makes and checks fighters without a GUI. The operations are the catalog's: [`gongfu.ops.omui.toml`](gongfu.ops.omui.toml), listed for CLI and MCP in [`media/ops/gongfu.ops.json`](media/ops/gongfu.ops.json), for reading in [`media/ops/gongfu.ops.md`](media/ops/gongfu.ops.md). Each is `gongfu-studio <verb> …` (`fighter.bake` is `gongfu-studio bake`, `roster.versus` is `gongfu-studio versus`): JSON on standard output, exit 0 done, 1 failed with its problems, 2 a wrong command.

## Before

1. A fighter is data: `<name>.fighter.gongfu.toml` (its moves, frame data, boxes), its look (`.look.gongfu.toml`: the drawn parts, bones and clips) and its pictures beside them. Read one that exists before writing one; problems are reported with the move and the field, at line and column.
2. `fighter.check` (a read): whether the baked hurtboxes are current. A stale one names the command that fixes it.

## Doing

1. Edit the fighter's files (moves, look) with any editor.
2. `fighter.bake` the directory: the hurtboxes follow the animation (a limb reaching out past the stance gets its box). Never edit a `.boxes.gongfu.toml` by hand.
3. Look at what you changed: `fighter.sheet` (every clip with its boxes, frame by frame), `stage.picture` for a stage with two fighters for scale.
4. New art: `art.cut` a sheet of parts into pieces, `art.picture` to key, trim and scale one picture.
5. Balance: `roster.versus` — bots play every pair; read each fighter's win rate and where its damage comes from.

## Verifying

- `fighter.check` reports every fighter `current`; a program's build (`gongfu.rules` runs `fighter.embed`) refuses stale boxes.
- The sheet shows the active frames' boxes where the limb is, not on the first frame.
- A match replays: the stage's `inputs` (both pads, frame by frame, and the checksum) played again give the same checksum.
- Balance claims cite the `roster.versus` report (rounds, seed), never one game.

## Not here

Playing a match is a host's: the fighting showcase's stage (`press`, `turn`, `inputs`, its catalog of actions in its contract). The rules of play (fixed-point simulation, rollback) are gongfu's library API.
