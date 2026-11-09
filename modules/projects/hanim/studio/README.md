# hanim.studio

hanim's animation tool: animation as a document an Agent or a person writes, checked, sampled and rendered without a GUI by the `hanim` command.

```sh
hanim check  examples/bounce.animation.omui.json                   # the plan: duration, tracks, markers — or every problem
hanim sample examples/bounce.animation.omui.json --at 0.5 --at 2   # where each object is, its scale, rotation, fill and bounds
hanim at     examples/bounce.animation.omui.json --at 1.9 --object star
                                                                   # the steps that govern that moment: JSON pointers, timing, JSON
hanim vary   examples/bounce.animation.omui.json --set /program/items/0/bounces --value 0 --value 4 --out /tmp/bounce
                                                                   # each variant a row at the same moments: /tmp/bounce.variants.png
hanim render examples/bounce.animation.omui.json --out /tmp/bounce --frames 16 --webp 30
                                                                   # /tmp/bounce-000.png …, .strip.png, .webp, .report.omui.json
```

Its operations are a catalog ([`studio.ops.omui.ts`](studio.ops.omui.ts), listed in [`media/ops`](media/ops)); how an Agent authors with it is [hanim's skill](../skill.omui.md). Every answer is one JSON object; exit 0 done, 1 the document or the work failed (its problems listed), 2 the command was wrong.

## The document (`<name>.animation.omui.json`, version 1)

- `size` `[w, h]` and `background` `"#rrggbb"`: the stage.
- `objects`: each an `id` (lowercase, digits, `-`, `_`), a `shape` — `{"rect": {"size": [w, h], "radius": r}}` (or `{"rect": [w, h]}`), `{"circle": r}`, `{"ellipse": [rx, ry]}`, `{"polygon": [[x, y], …]}`, `{"star": {"points": 5, "outer": r, "inner": r}}`, `{"path": "M0 30 C-42 6 … Z"}` (SVG path data, arcs included, several contours for holes), about its origin — a `fill` and/or a `stroke` (`{"color", "width"}`), where it stands (`at`), `rotation` (degrees), `scale` (a factor or `[x, y]`). `hidden`: drawn only while it moves (a morph's target).
- `program`: one step. `{"do": …}` is one of
  - `sequence`, `parallel` (`items`), `stagger` (`items`, `step` seconds), `wait` (`duration`), `loop` (`item`, `count`, `alternate`), `marker` (`name`);
  - `move_to` / `move_by` (`to` / `by` `[x, y]`), `rotate_to` / `rotate_by` (`degrees`), `scale_to` / `scale_by` (`to` / `by`), `fill_to` (`color`), `fade_to` (`opacity`), `morph` (`into` another object's shape) — each with `duration` (default 0.5 s) and `ease` (`"in_out_cubic"` default, `"out_back"`, `"cubic-bezier(.2, 0, 0, 1)"`, `"steps(4, jump-end)"` …) or a `spring` (`stiffness`, `damping`, `mass`: as long as it takes to settle);
  - `drop` (`by` `[x, y]`, `duration`, `bounces`, `restitution`, `squash`, `stretch`): falls, bounces lower each time, squashes as it lands;
  - `pulse` (`scale`), `shake` (`amplitude` `[x, y]`), `wobble` (`degrees`);
  - hanim's recipe library, each with `object`, `duration`, `ease` and its one parameter: entrances `fade_in`, `rise_in` / `sink_in` (`distance`), `slide_in` (`from` `[x, y]`), `pop_in`, `zoom_in`, `spin_in` (`degrees`), `unfold_in`, `write_in` (a stroked object drawn along), `drop_in` (`height`); exits `fade_out`, `rise_out` / `sink_out` (`distance`), `slide_out` (`to`), `pop_out`, `zoom_out`, `spin_out` (`degrees`), `fold_out`, `unwrite`; cues `bounce` (`height`), `flash` (`times`), `heartbeat` (`scale`), `rubber_band`, `swing` (`degrees`), `tada`, `nudge` (`by`); idle loops `bob` (`amplitude`), `breathe` (`scale`), `spin`, `sway` (`degrees`), `orbit` (`radius`), `blink` — each with `period` and `count` (1 by default); moves `arc_to` (`to`, `bend`), `swap` (`with` another object, `by` the way from one to the other, `bend`), `reorder` (`objects`, `by` an offset each, `step`).

A sample says each object's `opacity` (a recipe's fade) beside its fill. `read` checks the whole document and reports every problem at its JSON pointer (`/program/items/2/ease: …`), or a syntax error at its line and column. Objects that do not move stand still behind those that do.

**Examples to take away**: [twenty documents](examples/README.md) — a loader, a toast, a success check, a like, a badge, an error shake, a card shuffle, a menu, a page transition, confetti, a logo reveal … — each composed from the recipe library, each kept valid and moving by the tests.

An object whose first step is an entrance is not on the stage before that entrance begins; objects are drawn in the document's order (its z order) whatever the program's order.

## How it runs

`Studio::open` makes the objects in an Engine of its own and the steps a hanim Program, then operates it through `hanim.control` — `animation.prepare`, `animation.play`, `animation.pause`, `animation.seek` — each call admitted by `mlibs.control` and listed in the answer. Any moment can be asked for in any order and gives the same numbers and the same pixels. Frames are drawn by `hanim.render.vector` into xrender's CPU raster.

Tests (`mcpp test -p hanim.studio`): the example's whole course sampled (the fall, two bounces lower each time, the landing, the travel, the star's spring overshooting and settling, the morph into the box); frames differing through the course and repeating exactly; every kind of problem reported where it is.

Its own package because its dependencies (JSON, image encoding, the CPU raster) are a tool's, not hanim's.
