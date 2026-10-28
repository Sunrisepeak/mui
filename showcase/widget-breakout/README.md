# showcase/widget-breakout

《组件打砖块：openmui 巡礼》 (S4, D-50): a breakout that is also a tour of openmui. Each level's play is one of its abilities ([design](../../.agents/docs/2026-10-29-mgame-s4-breakout-design.md)).

| Package | |
|---|---|
| `openmui.showcase.widget_breakout` (here) | The game: the field (`field.contract.omui.toml`) over a level's bricks — real buttons, switches, sliders and cards laid out by openmui and read into the simulation where they stand; every hit changes the brick for real; Tab's semantic lens shows each brick's role and id, the hidden ones, and the tree's latest changes; an agent plays it through the field's actions (`launch`, `move_to`, `step`, `rewind`…). `omui-breakout` runs it |
| [sim](sim) | The deterministic simulation: paddles, swept balls, bricks, gifts, score and lives, rewindable |

Level 1 (semantics) is playable; levels 2–6 come next.
