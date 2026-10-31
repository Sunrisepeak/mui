# openmui.showcase.widget_breakout.sim

The breakout's simulation (S4; [design](../../../.agents/docs/2026-10-29-mgame-s4-breakout-design.md) §5, §8). It is a pure function of the state and the frame's actions, at 120 steps a second.

```cpp
import openmui.showcase.widget_breakout.sim;

omui::breakout::State s = omui::breakout::start(room);    // bricks from the room's layout
omui::breakout::step(s, {player, agent});                   // one frame
for (std::uint32_t i = 0; i < s.events; ++i) show(s.event[i]);   // what happened in it
mgame::Timeline<omui::breakout::Game> t(s);                 // rewind, replay
```

- **A frame:**
  - The paddles move: an axis, or toward the pointer. This frame's input moves them this frame, hit-stop or not.
  - A ball on a paddle rides it. The launch goes along the aim, which sweeps to and fro (±50°) while the ball waits, unless an agent holds it (`Input::aim`).
  - A ball's spin turns it a little each step and dies away. A paddle gives spin by its motion as it returns the ball, and adds that motion across; where on the paddle the ball met still steers it, up to 60°.
  - Each ball moves from one collision to the next (`mgame.physics`): walls, paddles by their motion relative to it, bricks. At most eight collisions a frame; if they run out, the ball stays put rather than passing through anything. Hits at one instant are ordered by index and their normals combined, so a seam between two bricks bounces like a face.
  - What a ball meets acts as what it is:
    - a brick loses durability and, broken, scores by the chain;
    - a switch flips and moves what is on its circuit (shutters open or close, kickers turn, a charged capacitor discharges);
    - a capacitor charges, and full it fires a beam along its row to the nearest wall or closed shutter each way;
    - a kicker sends the ball where it points (never back into itself);
    - a core, broken, shuts the room down in a widening wave.
  - A beam damages bricks by its charge, flips switches and charges capacitors by one, so beams chain; a capacitor that fired cools down, so two never fire each other for ever.
  - The chain since the ball left the bottom paddle is banked as sparks when the ball comes back to it (three or more). The last ball lost loses it. `Input::pay` spends sparks: a rewind's cost, an input so a replay pays it again.
  - Two paddles: alternate returns ready the finisher, a beam up the ball's column from the next bottom return.
  - Hit-stop on cores, beams and a first breakthrough into a tall room's upper chamber: nothing but the paddles moves.
  - Capsules fall and are caught: wide, more balls, slow motion, piercing, the magnet (the paddle holds the ball for another aim). What drops by chance is the rules' mask.
  - Never along a row for ever: a bounce keeps a ball climbing or falling, and after six seconds of nothing moving the next wall turns it.
- **One block of plain data** (`mgame.world` for bricks, balls and capsules). A timeline rewinds and replays it to the same bytes, and `checksum` compares plays. The frame's events live in the state, so a rewound frame's effects are never played twice.
- **`trace`:** the path a ball would take among the walls and bricks as they stand: the aim's forecast, and bots' landing prediction.
- **Determinism:** only exactly rounded arithmetic, no contraction or fast math. A whole game's checksum is pinned (the same on GCC, LLVM and wasm).

It knows nothing of openmui: the game (the bricks as openmui components) reads its events.
