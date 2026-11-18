# mlibs.dynamics

The generic solver core, with no domain in it: the shapes a solver collides with and the queries on them, and the XPBD family of 2D soft bodies — deterministic, stepped on [mlibs.simulation](../simulation)'s timeline, with no renderer, no UI and no materials. It moved here from the engine's [xengine.dynamics](../../projects/xengine/kernel/dynamics) (implementation plan A5, D-231): hanim's engine steps its bodies with it, so it belongs to the foundation; the engine's dynamics builds its families, its materials and its agent face on it.

```cpp
import mlibs.dynamics;
namespace dyn = mlibs::dynamics;

if (auto hit = dyn::sweep(ball.at, ball.radius, ball.velocity * left, brick)) {   // collision
    ball.at += ball.velocity * (left * hit->t);
    ball.velocity = dyn::reflect(ball.velocity, hit->normal);
}
auto body = dyn::SoftBody::ring(outline, {.mass = 1}, ground);                  // xpbd
mlibs::simulation::Timeline<dyn::SoftBodyModel> history(*body, 1200, 60);       // replayable
history.step({});
std::println("{}", dyn::explain(history.state()));                              // inspect
```

| Part | What |
|---|---|
| `collision` | Half-planes, one-way ledges, boxes; swept collision (a moving circle against a box, exact); `depth`, `lands` |
| `xpbd` | Soft bodies: rings of particles held by edges, area and shape matching; gravity, push, walk, hop, a dragging hand; half-planes and ledges with restitution and friction; `stages()` |
| `inspect` | `report(body)` (centre, bounds, area against rest, fastest speed, contacts by kind) and `explain(body)` in words, read between steps |

What a body is made of (a material's mechanical columns) is the engine's: `xengine.dynamics.matter`'s `params_of`. The bodies as an agent's subject are the engine's too (`xengine.dynamics.agent`), and so are the conformance cases (`xengine.dynamics.cts`, contracts `mlibs.dynamics.collision@1` and `mlibs.dynamics.xpbd@1`).

## Collision

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

## Soft bodies (xpbd)

A `SoftBody` is a ring of particles held by its edges' lengths, its enclosed area and the shape it was made in (shape matching; `upright` keeps the shape it springs back to unturned), stepped by small-step XPBD under gravity against half-planes and one-way ledges, with restitution and friction toward a walking speed at its feet, a push and a hop; a `Pin` is a dragging hand. `SoftBodyModel` is a `mlibs::simulation` model, so a body runs in a `Timeline` with checkpoints and exact replay; stepping allocates nothing. hanim's `Engine::simulate` makes any part of any object such a body and carries its geometry (and another figure later: `Engine::carry`) on a cage over the ring.

## Agent surface

A library: it has no subject of its own. A soft body is an agent's subject in the engine's tier, [xengine.dynamics](../../projects/xengine/kernel/dynamics)' `body` (state, observation, control, time, preview, budgets); this package gives it `report` and `explain`.

Run `mcpp test -p mlibs.dynamics`: swept collision to the same bits; soft bodies dropped, landed on ledges, walked, kept upright and replayed bit for bit.
