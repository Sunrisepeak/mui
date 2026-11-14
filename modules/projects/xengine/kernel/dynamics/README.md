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
| `rigid3` | 3D rigid bodies: Jolt Physics 5.6 under the same contract — boxes, spheres, capsules, hulls, meshes as ground, compounds, sensors; joints (fixed, hinge, slider, distance, point, cone) and walking characters as data on the world; contacts and where characters stand as a step's output; rays, shapes and shape casts asked; save / restore / branch bit for bit; an agent's subject (the features `rigid3`, `rigid3-agent`) | M6.4 |
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
world->touching(crate, out);               // whom it touches now: the state, not the last step's events
const auto saved = world->save();          // the whole state at a step boundary (Box2D's snapshot image)
auto branch = world->branch();             // a world of its own from here: world.branch
world->restore(saved);                     // rollback: every id still names its body
```

- **Rollback and branches, bit for bit**: a saved state restored and run again is the uninterrupted run's bits; a world made from it runs on alike; saving changes nothing (`xengine.dynamics.cts` rigid_test). A save is valid for the same Box2D version only — upstream calls its image format experimental — so it serves rollback, undo and branches within a run; what is kept across versions is the application's own state.
- **The family's hard constraints are a suite** (`xengine.dynamics.cts`' `declare_rigid`, contract `xengine.dynamics.rigid@1`): the world contract of `mlibs.cts` and ids that name the same body across an image and in a branch, against a small adapter — Box2D's today (9 passed), another solver's for three dimensions tomorrow. An image cut short or damaged is refused with the world as it was: Box2D's bytes lie under a length and a checksum of ours, since its own restore empties the world before it reads. One thing it has not, and says so (`canonical-image`, unsupported): two worlds at one state have one hash but not one image, byte for byte — Box2D writes its structures as they lie in memory. Compare states by `state_hash`.
- **The same bits everywhere**: Box2D is cross-platform deterministic built as here (no contraction, no fast math, its own trigonometry): the wrap's CI checks one scene on Linux x86-64 and arm64, macOS, Windows and wasm32 with 1 to 8 workers.
- **Numbers in**: a material's mechanical columns (`Material`: density, friction, restitution) — what `params_of` reads from xengine.matter's table.

## Rigid bodies in three dimensions (rigid3)

The 3D rigid family is Jolt Physics 5.6 (MIT; `mcpplibs.mwrap.jolt`, built cross-platform deterministic) under the same contract as the 2D one — the feature `rigid3`, module `xengine.dynamics.rigid3`, so a consumer that does not take it links no Jolt (D-170, D-176). Its numbers are `mlibs.math`'s `Vec3` and `Quat`.

```cpp
namespace rigid3 = xengine::dynamics::rigid3;
auto world = rigid3::World::make();                                                    // 1/60 s, gravity −9.81 along y
const auto crate = world->add({.at = {0, 4, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), rigid3::material_of(*table, *table->material("wood")));
world->step();
for (const rigid3::Contact& c : world->contacts()) { /* who met whom (ids in order), where, along which normal, how fast */ }
const auto hit = world->ray({0, 20, 0}, {0, -20, 0});                                 // the first body a segment meets
world->overlapping(rigid3::Shape::sphere(1), {0, 1, 0}, {}, found);                  // what a shape placed there overlaps
const auto saved = world->save();                                                      // every body as made, and Jolt's state
auto branch = world->branch();
world->restore(saved);                                                                 // a body removed since is back, one added since gone

rigid3::Joint door = rigid3::Joint::hinge(panel, {}, {-7, 1, 0}, {0, 1, 0});         // `panel` turns about y on a hinge to the world
door.lower = -0.5f, door.upper = 0.5f, door.motor = {.mode = rigid3::Motor::Mode::velocity, .target = 1, .most = 200};
const auto hinge = *world->join(door);
world->drive(hinge, rigid3::Motor::Mode::position, 0.3f);                             // from the next step
const auto walker = *world->add(rigid3::CharacterDef{.at = {1, 0, 0}});              // its feet; 1.8 m tall, climbs 50°, steps 0.4 m
world->walk(walker, {2, 0, 0}), world->jump(walker, 5);
world->step();
for (const rigid3::CharacterState& c : world->grounds()) { /* on the ground or in the air, on which body, the ground's normal */ }
const auto first = world->cast(rigid3::Shape::sphere(0.5f), {}, {0, 10, 0}, {0, -10, 0}); // the first body a shape swept meets
```

- **Bodies**: boxes, spheres, capsules, convex hulls; a triangle mesh on a fixed body (terrain, buildings). `Kind::kinematic` is moved by its velocity alone. `bullet` sweeps a fast body against what it would pass through.
- **Compounds and sensors**: `Shape::compound(parts)` makes several shapes one body (Jolt's static compound); `compound(parts, true)` a mutable one whose parts `move_part` moves later (the image carries the shape as it is). A body made with `.sensor = true` pushes nothing and reports what overlaps it as contacts marked `sensor`; it senses moving bodies.
- **Joints are data on the world**: `join(Joint)` — fixed, hinge (limits, a motor toward a speed or an angle), slider (limits, a motor), distance (a rope: lower and upper lengths), point (a ball joint), cone — between two bodies or a body and the world; `joint(id)` reads a hinge's angle, a slider's place, a distance joint's length and the motor; `drive` changes the motor; removing a body removes its joints. The image carries each joint's settings in the bodies' own frames as Jolt writes them, and Jolt's order of them, so a restore and a branch made from bytes alone step on to the same bits. A limit is met in the step a body arrives at it: past it by at most that step's travel, back within 1% as the solver settles (the parts test bounds both).
- **Characters**: `add(CharacterDef)` — Jolt's CharacterVirtual: a capsule walked by `walk(velocity)` and `jump(speed)`, up steps and the slopes it can climb, held back by steeper ones, stuck to the floor going down; it pushes bodies and is not pushed by them, and a ray does not meet it. Where each stands is a step's output (`grounds()`, in id order). Its place, velocity, ground state and walk are in the image.
- **Materials from the table**: `material_of(table, material)` reads the mechanical columns (`density` in kg/m³, `friction`, `restitution`) — a cubic metre of stone weighs its density.
- **Contacts are a step's output**: what began and ended touching in the step, named by the world's ids (`one` < `other`), in their order, each pair once a kind — the same list with any number of workers.
- **Ids are the world's**: 1 upwards in the order made, carried by the image with how each body was made (its kind, material and shape's bytes), so a restore brings back a body removed since under its id and a branch made from bytes alone has every body.
- **The family's hard constraints** (`xengine.dynamics.cts`' `declare_rigid`, contract `xengine.dynamics.rigid@1`): 10 of 10 pass on Jolt — the world contract and the ids across an image and in a branch, `canonical-image` among them (two worlds at one state write one image, byte for byte; Jolt writes its state value by value, where Box2D's family reports it unsupported).
- **The same bits everywhere**: the wrap's CI checks one scene on Linux x86-64 and arm64, macOS, Windows and wasm32, in the dev and release profiles, with 0, 1 and 4 workers; here a run with 1 and 3 workers is the single-threaded run's bits — with every joint form, a character, compounds and a sensor in the world too (`rigid3_parts_test`).
- Beside the solver it wraps: `simulation/rigid3-tower-512` in `tests/bench` steps the same bodies on a `World` and on Jolt called directly (`rigid3::Direct`), taking turns.

## Soft bodies (xpbd)

A `SoftBody` is a ring of particles held by its edges' lengths, its enclosed area and the shape it was made in (shape matching; `upright` keeps the shape it springs back to unturned), stepped by small-step XPBD under gravity against half-planes and one-way ledges, with restitution and friction toward a walking speed at its feet, a push and a hop; a `Pin` is a dragging hand. `SoftBodyModel` is a `mlibs::simulation` model, so a body runs in a `Timeline` with checkpoints and exact replay; stepping allocates nothing. hanim's `Engine::simulate` makes any part of any object such a body and carries its geometry (and another figure later: `Engine::carry`) on a cage over the ring.

## Looking at it, holding it to its contract

- `inspect`: `report(body)` (centre, bounds, area against rest, fastest speed, contacts by kind) and `explain(body)` in words, read between steps — an agent pauses a timeline, steps it once and reads.
- [cts](cts): `xengine.dynamics.cts` runs every family's cases (collision, xpbd, material) on a subject — the CPU reference today, a SIMD or GPU tier later — on `mlibs.cts`: passed, failed or unsupported.
- The rigid family beside the solver it wraps: `simulation/rigid-pyramid-20` and `simulation/rigid-rain-1000` in `tests/bench` step the same bodies on a `World` and on Box2D called directly (`rigid::Direct`), in the same run and taking turns, every body at the same place in both — the wrapper costs 1.6% and 3.4% (release, locally); a budget holds it under 10%.
- Benchmarks: `dynamics/softbody-24` and `-96` in `tests/bench` (zero allocations a step; a frame's budget for a figure's softness).

Run `mcpp test -p xengine.dynamics -p xengine.dynamics.cts`: swept collision to the same bits, soft bodies dropped, landed on ledges, walked, kept upright and replayed bit for bit, the material table; both rigid families held to their hard constraints (`rigid_test`, `rigid3_test`, and `rigid3_parts_test` with joints, a character, compounds and a sensor).

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
| Not here | **Causes** as a record — who pushed it, which contact turned it — and **source** (from a particle's place back to what moved it): `capabilities` says so, and the contract suite reports them unsupported; `body.contacts` says what the last step touched, which is not that. They come with the kernels' record of causes (the plan's T1.11). The 2D rigid family has no subject of its own: its worlds are reached through what holds them (`xengine.cellular`'s `MatterWorld`) |

A 3D rigid world is a subject too, module `xengine.dynamics.rigid3.agent` — the feature `rigid3-agent` (it implies `rigid3` and `agent`). Its catalog is [rigid3.ops.omui.ts](rigid3.ops.omui.ts) (the table `src/rigid3/agent/catalog.cppm` and the listings `rigid3.ops.json`, `.md` are `omui-gen ops`'s).

| | |
|---|---|
| Subject and addresses | `rigid3`, interface `xengine.dynamics.rigid3@0.1`. A body is its id (a number, 1 upwards in the order made); a place is the world's metres, `[x, y, z]` with y up; a frame is a step played |
| State | The `World`; the hash covers every body's place, turn and velocities, bit for bit |
| Observation | `rigid3.observe`: the frame, the hash, how many bodies (and how many awake), joints and characters, what began and ended touching in the last step (where, along which normal, how fast; a sensor's overlaps marked `sensor`); `rigid3.bodies` for each body; `rigid3.joints` for each joint's form, bodies, value and motor; `rigid3.where` for a character's feet, velocity, walk and ground; `rigid3.ground`: what each character stood on after the last step; `rigid3.ray` and `rigid3.cast` (a sphere, box or capsule swept): the first body met |
| Control | `rigid3.push`: an impulse on a body, through its centre of mass or at a point; `rigid3.drive`: a hinge's or slider's motor; `rigid3.move` and `rigid3.jump`: a character's walk and jump — each given on the next step, taken back by seeking |
| Time | `rigid3.step`, `rigid3.seek` back within the steps kept — a checkpoint (the world's image) every 60 steps and the inputs given since (pushes, drives, walks, jumps), played again: the same bits as the run that stopped there |
| Preview | `rigid3.branch`: the world some steps ahead, a body pushed first if one is given, observed as `rigid3.observe` does; the world itself untouched (a branch of it is stepped) |
| Budgets and tiers | `rigid3.observe` within a `budget` of bytes (contacts left out from the last, `more` says how many); `Options`: `keep` steps of history, a checkpoint `every` so many, `most_steps` a request |
| Not here | **Causes** and **source**, as for the soft body: reported unsupported by the contract suite (10 passed, 3 unsupported) |

```sh
mcpp test -p xengine.dynamics.cts     # agent_test, rigid3_agent_test: the agent contract's suite on a body and on a 3D world, then pushes, motors, characters, seeks and branches
```

The subject's tests are [cts](cts)'s, which turns the feature on: a feature is tested where it is on.
