# omui.showcase.widget_breakout.headless

The breakout on its own (D-162): [the simulation](../sim) as [xengine.runtime](../../../modules/projects/xengine/game/runtime) plays it. No omui in the program (lint `closure`): the game is found by name at an endpoint of the agent kernel and played by an agent as a person plays — what it is shown (the paddle, the balls, the bricks standing, the score; not the hidden caches), the same actions (an axis, the launch button), a tenth of a second's reaction, ten actions a second.

```cpp
import omui.showcase.widget_breakout.headless;
omui::breakout::Headless game(omui::breakout::start(level));   // xengine::runtime::Runtime<omui::breakout::Play>
```

Its test plays a wall of bricks over a connection for up to forty seconds of the game's time, twice: bricks broken and a score, no action refused, and the same state hash both times; `world.branch` shows the ball launched 240 steps ahead while the game still waits.

```sh
mcpp test -p omui.showcase.widget_breakout.headless
```
