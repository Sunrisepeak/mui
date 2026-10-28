# mgame.physics

Swept collision for games that rewind and roll back ([S4 design](../../.agents/docs/2026-10-29-mgame-s4-breakout-design.md) G-2).

```cpp
import mgame.physics;

if (auto hit = mgame::sweep(ball.at, ball.radius, ball.velocity * left, brick)) {
    ball.at += ball.velocity * (left * hit->t);           // to the moment it touches
    ball.velocity = mgame::reflect(ball.velocity, hit->normal);
}
```

- **Sweeps:** when a moving circle first touches a box, exactly. The circle's centre travels toward the box grown by its radius: four faces moved out, and four quarter circles at the corners. The earliest crossing in the move is the hit, with its side's normal; at a corner, the normal points from the corner to the centre.
  - A fast ball meets a thin box that a sampled step would skip.
  - A ball already overlapping a box and moving further in meets it at once, on its nearest side.
- **Event by event:** a game moves a ball from one collision to the next within a step. Between collisions it is where a formula says, so any instant can be computed, not sampled. This is what rewinding needs.
- **Only real hits:** a contact counts when the circle moves into the side it meets. A graze along a face, or a tangent past a corner, is no hit, so a ball never bounces at zero time again and again.
- **Corners keep their precision:** the first root is taken as c / (−b′ + √disc), which does not cancel when the circle starts a hair away.
- **The same bits everywhere:** doubles with only + − × ÷ and √, which IEEE 754 rounds exactly, as hanim does.
  - Lint's determinism rule checks that it never calls libm's transcendentals.
  - It is built without contraction (`-ffp-contract=off`: no fused multiply-add) and without fast math (no reassociation, no flushing of subnormals).
  - On x86-64 (SSE2), arm64 and wasm the rounding mode stays the default and there is no extended intermediate precision.
  - It makes no NaN: a zero vector normalizes to zero.

Box2D v3 (in mcpp-index as `mcpplibs.mwrap.box2d`) suits games that do not rewind: its world cannot be saved as one block. It comes later as a plugin.

The standard library alone.
