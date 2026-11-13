# xengine.dynamics

Every solved simulation in one owner (D-133): collision and its queries and the solver families — deterministic, stepped on [mlibs.simulation](../../../../base/simulation)'s timeline, with no renderer and no UI. The architecture and its phases: [simulation, dynamics and the compute layer](../../../../../.agents/docs/2026-11-08-simulation-dynamics-architecture-design.md).

```cpp
import xengine.dynamics;
namespace dyn = xengine::dynamics;

if (auto hit = dyn::sweep(ball.at, ball.radius, ball.velocity * left, brick)) {   // collision
    ball.at += ball.velocity * (left * hit->t);
    ball.velocity = dyn::reflect(ball.velocity, hit->normal);
}
auto table = xengine::matter::standard();                                        // what things are made of: xengine.matter (import xengine.dynamics.matter;)
auto body = dyn::SoftBody::ring(outline, dyn::params_of(*table, *table->material("jelly"), area), ground);   // xpbd
mlibs::simulation::Timeline<dyn::SoftBodyModel> history(*body, 1200, 60);       // replayable
history.step({});
```

| Part | What | State |
|---|---|---|
| `collision` | Half-planes, one-way ledges, boxes; swept collision (a moving circle against a box, exact); `depth`, `lands` | from the former `xengine.physics`; planes and ledges from the soft bodies |
| materials | Not here: what a body is made of is [xengine.matter](../matter)'s table (D-165 — the one table physics, chemistry and the lattice share). `params_of(table, material, area)` reads its mechanical columns (`density` over water's × the area as the mass, `friction`, `restitution`, `compliance`, `shape_stiffness`); it is the package's feature `matter` (module `xengine.dynamics.matter`, taking the matter kernel alone, no JSON): a solver takes numbers, and only a consumer that has materials takes the table — hanim's engine does not | moved from `xengine.dynamics.material` (2026-11-13); the standard table has its rows (rubber, wood, stone, jelly, cloth, sand, water), the same numbers |
| `xpbd` | Soft bodies: rings of particles held by edges, area and shape matching (optionally upright); gravity, push, walk, hop, a dragging hand; half-planes and ledges with restitution and friction; `stages()` says a step's stages in order | from the former `xengine.simulation.bodies` |
| `rigid` | 2D rigid bodies: Box2D v3.2 under this contract — fixed steps, contacts, save / restore / branch bit for bit (the feature `rigid`) | P1, M6.2 |
| `articulation`, fluids, FEM, MPM | Later solver families (each its own feature) | P2–P3 of the design |

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

## Rigid bodies (rigid)

The 2D rigid family is Box2D v3.2 (MIT; `mcpplibs.mwrap.box2d`) under this package's contract — the feature `rigid`, module `xengine.dynamics.rigid`, so a consumer that does not take it links no Box2D (D-170, D-176: the strongest independent solver, reused under our contract). Box2D solves; ours is the shape every family shares:

```cpp
namespace rigid = xengine::dynamics::rigid;
auto world = rigid::World::make({.gravity = {0, -10}});                              // 1/60 s, 4 substeps
auto crate = world->add({.at = {0, 4}}, rigid::Shape::box(0.5f, 0.5f), {.density = 1, .friction = 0.6f});
world->step();
for (const rigid::Contact& c : world->contacts()) { /* who met whom, where, how fast */ }
const auto saved = world->save();          // the whole state at a step boundary (Box2D's snapshot image)
auto branch = world->branch();             // a world of its own from here: world.branch
world->restore(saved);                     // rollback: every id still names its body
```

- **Rollback and branches, bit for bit**: a saved state restored and run again is the uninterrupted run's bits; a world made from it runs on alike; saving changes nothing (`xengine.dynamics.cts` rigid_test). A save is valid for the same Box2D version only — upstream calls its image format experimental — so it serves rollback, undo and branches within a run; what is kept across versions is the application's own state.
- **The family's hard constraints are a suite** (`xengine.dynamics.cts`' `declare_rigid`, contract `xengine.dynamics.rigid@1`): the world contract of `mlibs.cts` and ids that name the same body across an image and in a branch, against a small adapter — Box2D's today (9 passed), another solver's for three dimensions tomorrow. An image cut short or damaged is refused with the world as it was: Box2D's bytes lie under a length and a checksum of ours, since its own restore empties the world before it reads. One thing it has not, and says so (`canonical-image`, unsupported): two worlds at one state have one hash but not one image, byte for byte — Box2D writes its structures as they lie in memory. Compare states by `state_hash`.
- **The same bits everywhere**: Box2D is cross-platform deterministic built as here (no contraction, no fast math, its own trigonometry): the wrap's CI checks one scene on Linux x86-64 and arm64, macOS, Windows and wasm32 with 1 to 8 workers.
- **Numbers in**: a material's mechanical columns (`Material`: density, friction, restitution) — what `params_of` reads from xengine.matter's table.

## Soft bodies (xpbd)

A `SoftBody` is a ring of particles held by its edges' lengths, its enclosed area and the shape it was made in (shape matching; `upright` keeps the shape it springs back to unturned), stepped by small-step XPBD under gravity against half-planes and one-way ledges, with restitution and friction toward a walking speed at its feet, a push and a hop; a `Pin` is a dragging hand. `SoftBodyModel` is a `mlibs::simulation` model, so a body runs in a `Timeline` with checkpoints and exact replay; stepping allocates nothing. hanim's `Engine::simulate` makes any part of any object such a body and carries its geometry (and another figure later: `Engine::carry`) on a cage over the ring.

## Looking at it, holding it to its contract

- `inspect`: `report(body)` (centre, bounds, area against rest, fastest speed, contacts by kind) and `explain(body)` in words, read between steps — an agent pauses a timeline, steps it once and reads.
- [cts](cts): `xengine.dynamics.cts` runs every family's cases (collision, xpbd, material) on a subject — the CPU reference today, a SIMD or GPU tier later — on `mlibs.cts`: passed, failed or unsupported.
- Benchmarks: `dynamics/softbody-24` and `-96` in `tests/bench` (zero allocations a step; a frame's budget for a figure's softness).

Run `mcpp test -p xengine.dynamics -p xengine.dynamics.cts`: swept collision to the same bits, soft bodies dropped, landed on ledges, walked, kept upright and replayed bit for bit, the material table.

## Agent surface

A soft body is a `Subject` of the agent kernel ([mlibs.agent](../../../../base/agent)), module `xengine.dynamics.agent` — the package's feature `agent` (`xengine.dynamics = { path = …, features = ["agent"] }`), so that the solver families taken alone stay the small tools and nothing more (hanim's engine takes them so); every operation is decided there first (openas). Its operations are a catalog, [body.ops.omui.ts](body.ops.omui.ts) (the table `src/agent/catalog.cppm` and the listings `src/agent/body.ops.json`, `.md` are `omui-gen ops`'s); how to use them, in order, is [skill.omui.md](skill.omui.md).

| | |
|---|---|
| Subject and addresses | `body`, interface `xengine.dynamics.body@0.1`. A particle is its index around the ring; a frame is a step played |
| State | The `SoftBody` (each particle's place and velocity), on `mlibs.simulation`'s timeline, copied whole; the hash covers every particle's place and velocity, bit for bit |
| Observation | `body.observe`: centre, bounds, area against its rest area, its fastest particle's speed, and what the last step touched (a particle on a plane or a ledge); `body.particles` when each one is needed; `body.contacts`: the same in words, with the stages a step goes through |
| Control | `body.push`: a force and a walk held from the next step on, a kick once — what a person's hand or a game's rule gives it; taken back by seeking |
| Time | `body.step`, `body.seek` back within the steps kept (what is stepped from there is a new branch); the same requests, the same bits |
| Preview | `body.branch`: the body some steps ahead under a given push, observed as `body.observe` does; the body itself untouched (a copy is stepped) |
| Budgets and tiers | `body.observe` within a `budget` of bytes (contacts left out from the last, `more` says how many; a budget below the least answer is refused with that size); `Options`: `most_steps` a request, `keep` steps of history |
| Not here | **Causes** as a record — who pushed it, which contact turned it — and **source** (from a particle's place back to what moved it): `capabilities` says so, and the contract suite reports them unsupported; `body.contacts` says what the last step touched, which is not that. They come with the kernels' record of causes (the plan's T1.11). The rigid families get their own subject when they land (M6.2) |

```sh
mcpp test -p xengine.dynamics.cts     # agent_test: the agent contract's suite on a resting body, then kicks, seeks and branches
```

The subject's tests are [cts](cts)'s, which turns the feature on: a feature is tested where it is on.
