# mgame.sim

The fixed-step simulation and its timeline ([S4 design](../../.agents/docs/2026-10-29-mgame-s4-breakout-design.md) G-3, G-4).

```cpp
import mgame.sim;

mgame::Clock clock(120);                              // steps a second
mgame::Timeline<Breakout> timeline(start, 3600, 60);  // 30 s kept, a snapshot each half second
for (int n = clock.advance(now); n > 0; --n) timeline.step(input);
draw(timeline.state(), clock.alpha());                 // between the last step and the next

timeline.seek(timeline.frame() - 240);                 // two seconds back, exactly
timeline.step(other);                                  // on from there: a new branch
```

- **The clock** (`Clock`): how many steps are due on its own clock. `catch_up` limits how many it takes at once: a stall past that is not owed afterwards. `scale` runs it slower or faster (slow motion). `alpha` is the fraction toward the next step, for drawing.
- **The timeline** (`Timeline<Game>`):
  - A game is a `State` and an `Input` of plain data, and `step(State&, const Input&)`.
  - It keeps a snapshot every `every` frames and every frame's input for the last `keep` frames.
  - `seek(f)` reaches any of those frames exactly: the snapshot before it, then its inputs played again.
  - Stepping on from there is a new branch: what came after it is forgotten.
  - Rewind, rollback and replay are all this.

The standard library alone.
