# xengine.dynamics

Every solved simulation in one owner (D-133): collision and its queries, the solver families, and the physical materials they share — deterministic, stepped on [mlibs.simulation](../../../../base/simulation)'s timeline, with no renderer and no UI. The architecture and its phases: [simulation, dynamics and the compute layer](../../../../../.agents/docs/2026-11-08-simulation-dynamics-architecture-design.md).

```cpp
import xengine.dynamics;
namespace dyn = xengine::dynamics;

if (auto hit = dyn::sweep(ball.at, ball.radius, ball.velocity * left, brick)) {   // collision
    ball.at += ball.velocity * (left * hit->t);
    ball.velocity = dyn::reflect(ball.velocity, hit->normal);
}
auto jelly = *dyn::MaterialTable::defaults().find("jelly");                    // materials
auto body = dyn::SoftBody::ring(outline, dyn::params_of(jelly, area), ground);  // xpbd
mlibs::simulation::Timeline<dyn::SoftBodyModel> history(*body, 1200, 60);       // replayable
history.step({});
```

| Part | What | State |
|---|---|---|
| `collision` | Half-planes, one-way ledges, boxes; swept collision (a moving circle against a box, exact); `depth`, `lands` | from the former `xengine.physics`; planes and ledges from the soft bodies |
| `material` | `PhysicalMaterial` (phase, density, friction, restitution, compliance, shape stiffness), `MaterialTable` with stable ids and defaults; `params_of` gives a soft body's | new (a render material is xrender's; the two meet by a surface type's id) |
| `xpbd` | Soft bodies: rings of particles held by edges, area and shape matching (optionally upright); gravity, push, walk, hop, a dragging hand; half-planes and ledges with restitution and friction; `stages()` says a step's stages in order | from the former `xengine.simulation.bodies` |
| `rigid`, `articulation`, fluids, FEM, MPM | Later solver families (each its own feature) | P1–P3 of the design |

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

Box2D v3 (in mcpp-index as `mcpplibs.mwrap.box2d`) suits games that do not rewind: its world cannot be saved as one block. It comes later as a plugin.

Box2D v3 (in mcpp-index as `mcpplibs.mwrap.box2d`) suits games that do not rewind; this package's own rigid family (P1) is measured against it.

## Soft bodies (xpbd)

A `SoftBody` is a ring of particles held by its edges' lengths, its enclosed area and the shape it was made in (shape matching; `upright` keeps the shape it springs back to unturned), stepped by small-step XPBD under gravity against half-planes and one-way ledges, with restitution and friction toward a walking speed at its feet, a push and a hop; a `Pin` is a dragging hand. `SoftBodyModel` is a `mlibs::simulation` model, so a body runs in a `Timeline` with checkpoints and exact replay; stepping allocates nothing. hanim's `Engine::simulate` makes any part of any object such a body and carries its geometry (and another figure later: `Engine::carry`) on a cage over the ring.

## Looking at it, holding it to its contract

- `inspect`: `report(body)` (centre, bounds, area against rest, fastest speed, contacts by kind) and `explain(body)` in words, read between steps — an agent pauses a timeline, steps it once and reads.
- [cts](cts): `xengine.dynamics.cts` runs every family's cases (collision, xpbd, material) on a subject — the CPU reference today, a SIMD or GPU tier later — on `mlibs.cts`: passed, failed or unsupported.
- Benchmarks: `dynamics/softbody-24` and `-96` in `tests/bench` (zero allocations a step; a frame's budget for a figure's softness).

Run `mcpp test -p xengine.dynamics -p xengine.dynamics.cts`: swept collision to the same bits, soft bodies dropped, landed on ledges, walked, kept upright and replayed bit for bit, the material table.
