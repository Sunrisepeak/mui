# showcase/widget-breakout

《组件打砖块：openmui 巡礼》 (S4, D-50): a breakout that is also a tour of openmui. Each level's play is one of its abilities ([design](../../.agents/docs/2026-10-29-mgame-s4-breakout-design.md)).

| Package | |
|---|---|
| `openmui.showcase.widget_breakout` (here) | The game: the field (`field.contract.omui.toml`) over a level's bricks — real buttons, switches, sliders and cards laid out by openmui and read into the simulation where they stand; every hit changes the brick for real; Tab's semantic lens shows each brick's role and id, the hidden ones, and the tree's latest changes; an agent plays it through the field's actions (`launch`, `move_to`, `step`, `rewind`…). `omui-breakout` runs it |
| [sim](sim) | The deterministic simulation: paddles, swept balls, bricks, gifts, score and lives, rewindable |

Levels 1 (semantics: the lens), 2 (styles: five packs, one play) 4 (time: a gate open once, R to rewind) and 5 (agents: a copilot plays the top paddle through the semantic tree and actions) are playable, 3 (canvas: a wall four screens tall, boxes far and live cards near) and 6 (languages: the gifts' rules are TypeScript and Lua scripts, edited and hot-reloaded in the workshop; Python next) are playable.
