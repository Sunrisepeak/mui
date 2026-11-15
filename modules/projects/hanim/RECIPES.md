# hanim's recipes, each through its course

Every recipe of the library (`hanim::anim::recipes()`) and the studio's moves, each drawn at eight moments from its start to its end with its defaults: a card on a 120 × 90 stage, one document a recipe (`{"do": "<recipe>", "object": "card"}`), the CPU reference's pixels. The strips are drawn by `hanim.studio`'s `recipes_test`, which holds the committed ones to what it draws now; a document writes the parameter by the name given here, angles in degrees.

## Entrances

| Recipe | Parameter (default) | Course |
|---|---|---|
| `fade_in` | — | ![fade_in](media/recipes/fade_in.png) |
| `rise_in` | `distance` (24) | ![rise_in](media/recipes/rise_in.png) |
| `sink_in` | `distance` (24) | ![sink_in](media/recipes/sink_in.png) |
| `slide_in` | `from` ([-40, 0]) | ![slide_in](media/recipes/slide_in.png) |
| `settle_in` | `scale` (0.96) | ![settle_in](media/recipes/settle_in.png) |
| `pop_in` | — | ![pop_in](media/recipes/pop_in.png) |
| `zoom_in` | — | ![zoom_in](media/recipes/zoom_in.png) |
| `spin_in` | `degrees` (-68.8°) | ![spin_in](media/recipes/spin_in.png) |
| `unfold_in` | — | ![unfold_in](media/recipes/unfold_in.png) |
| `write_in` | — | ![write_in](media/recipes/write_in.png) |
| `drop_in` | `height` (160) | ![drop_in](media/recipes/drop_in.png) |
## Exits

| Recipe | Parameter (default) | Course |
|---|---|---|
| `fade_out` | — | ![fade_out](media/recipes/fade_out.png) |
| `rise_out` | `distance` (24) | ![rise_out](media/recipes/rise_out.png) |
| `sink_out` | `distance` (24) | ![sink_out](media/recipes/sink_out.png) |
| `slide_out` | `to` ([40, 0]) | ![slide_out](media/recipes/slide_out.png) |
| `settle_out` | `scale` (0.96) | ![settle_out](media/recipes/settle_out.png) |
| `pop_out` | — | ![pop_out](media/recipes/pop_out.png) |
| `zoom_out` | — | ![zoom_out](media/recipes/zoom_out.png) |
| `spin_out` | `degrees` (68.8°) | ![spin_out](media/recipes/spin_out.png) |
| `fold_out` | — | ![fold_out](media/recipes/fold_out.png) |
| `unwrite` | — | ![unwrite](media/recipes/unwrite.png) |
## Cues

| Recipe | Parameter (default) | Course |
|---|---|---|
| `pulse` | `factor` ([1.08, 1.08]) | ![pulse](media/recipes/pulse.png) |
| `shake` | `amplitude` ([8, 0]) | ![shake](media/recipes/shake.png) |
| `wobble` | `degrees` (6.9°) | ![wobble](media/recipes/wobble.png) |
| `bounce` | `height` (28) | ![bounce](media/recipes/bounce.png) |
| `flash` | `times` (2) | ![flash](media/recipes/flash.png) |
| `heartbeat` | `scale` (1.15) | ![heartbeat](media/recipes/heartbeat.png) |
| `rubber_band` | — | ![rubber_band](media/recipes/rubber_band.png) |
| `swing` | `degrees` (14.9°) | ![swing](media/recipes/swing.png) |
| `tada` | — | ![tada](media/recipes/tada.png) |
| `nudge` | `by` ([8, 0]) | ![nudge](media/recipes/nudge.png) |
## Idle loops

| Recipe | Parameter (default) | Course |
|---|---|---|
| `bob` | `amplitude` (6); `period` 2.4 s, `count` 1 | ![bob](media/recipes/bob.png) |
| `breathe` | `scale` (1.04); `period` 3 s, `count` 1 | ![breathe](media/recipes/breathe.png) |
| `spin` | —; `period` 1.2 s, `count` 1 | ![spin](media/recipes/spin.png) |
| `sway` | `degrees` (4.6°); `period` 2.4 s, `count` 1 | ![sway](media/recipes/sway.png) |
| `orbit` | `radius` (16); `period` 2 s, `count` 1 | ![orbit](media/recipes/orbit.png) |
| `blink` | —; `period` 1 s, `count` 1 | ![blink](media/recipes/blink.png) |

## Moves

| Recipe | As drawn | Course |
|---|---|---|
| `drop` | `by` [0, 24], `bounces` 2 | ![drop](media/recipes/drop.png) |
| `arc_to` | `to` [96, 45] | ![arc_to](media/recipes/arc_to.png) |
| `swap` | `with` a second card, `by` [36, 0] | ![swap](media/recipes/swap.png) |
| `reorder` | three cards, `by` an offset each | ![reorder](media/recipes/reorder.png) |
