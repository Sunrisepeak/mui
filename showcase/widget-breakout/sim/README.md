# openmui.showcase.widget_breakout.sim

The breakout's simulation (S4; [design](../../../.agents/docs/2026-10-29-mgame-s4-breakout-design.md) §5). It is a pure function of the state and the frame's actions, at 120 steps a second.

```cpp
import openmui.showcase.widget_breakout.sim;

omui::breakout::State s = omui::breakout::start(level);   // bricks from the level's layout
omui::breakout::step(s, {player, agent});                   // one frame
for (std::uint32_t i = 0; i < s.events; ++i) show(s.event[i]);   // what happened in it
mgame::Timeline<omui::breakout::Game> t(s);                 // rewind, replay
```

- **A frame:**
  - The paddles move: an axis, or toward the pointer. This frame's input moves them this frame.
  - A ball on a paddle rides it until launched.
  - Each ball moves from one collision to the next (`mgame.physics`): walls, paddles by their motion relative to it, bricks. At most eight collisions a frame; if they run out, the ball stays put rather than passing through anything.
  - Hits at one instant are ordered by index and their normals combined, so a seam between two bricks bounces like a face.
  - Where a ball meets the paddle steers it, up to 60°.
  - A hit brick loses durability. Broken, it scores by the combo and sometimes drops a capsule: wide, three balls, slow motion, piercing.
  - A ball out of the field is lost; the last one costs a life.
- **One block of plain data** (`mgame.world` for bricks, balls and capsules). A timeline rewinds and replays it to the same bytes, and `checksum` compares plays. The frame's events live in the state, so a rewound frame's effects are never played twice.
- **Determinism:** only exactly rounded arithmetic, no contraction or fast math. A whole game's checksum is the same on GCC, LLVM and wasm.

It knows nothing of openmui: the game (the bricks as openmui components) reads its events.
