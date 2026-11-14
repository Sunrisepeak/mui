# xengine.nav

Navigation for games: a navigation mesh built from triangles for walkers of a given size, paths across it, and crowds walking it — Recast & Detour (zlib, through mwrap's `mcpplibs.mwrap.recastnavigation`) under this package's contract: the mesh, its obstacles and links and every walker of a crowd are one world that steps, saves, restores and branches to the same bits.

The game layer's (D-161): what moves where in a game is the game's; nothing here knows omui, xrender or a body's solver. The ground is given as data — triangles from a level, or a 3D rigid world's fixed shapes turned into triangles by the caller — so that navigation depends on no physics.

## Agent surface

A navigation world is a `Subject` of the agent kernel ([mlibs.agent](../../../../base/agent)), module `xengine.nav.agent` — the package's feature `agent`, so a game that only asks paths links no agent kernel. Its operations are a catalog, [nav.ops.omui.ts](nav.ops.omui.ts) (the table `src/agent/catalog.cppm` and the listings `src/agent/nav.ops.json`, `.md` are `omui-gen ops`'s); how to use them, in order, is [skill.omui.md](skill.omui.md).

| | |
|---|---|
| Subject and addresses | `nav`, interface `xengine.nav@0.1`. A walker of the crowd, an obstacle and a link are each named by an id (a number, 1 upwards in the order made); a place is the world's metres, `[x, y, z]` with y up; a frame is a step played |
| State | The `World`: the ground's triangles and the build's numbers, the mesh's tiles (each with its generation), the obstacles and links, and every walker (place, velocity, target, the corridor of polygons it follows, how far across a link); the hash covers all of it, bit for bit |
| Observation | `nav.observe`: the frame, the hash, the mesh's tiles and polygons, how many walkers walk, wait or have arrived; `nav.where` for each walker's place, velocity, target, how it is going and the corners ahead of it; `nav.path`: the way between two places — its corners (those where a link begins marked), its length, whether it reaches, and how many polygons it crosses; `nav.nearest`: the nearest place on the mesh; `nav.ray`: how far a straight walk along the mesh goes and what stops it |
| Control | `nav.build` the mesh anew from triangles; `nav.add` and `nav.remove` a walker; `nav.target` sends one to a place, or stops it; `nav.obstacle` places or takes away an obstacle (the tiles under it are built again); `nav.link` joins two places a walker could not walk between (a jump, a ladder) or takes a link away — each given at once, before the next step, and taken back by seeking |
| Time | `nav.step`, `nav.seek` back within the steps kept — a checkpoint (the world's image) every 60 steps and the inputs given since, played again: the same bits as the run that stopped there |
| Preview | `nav.branch`: the world some steps ahead, a walker sent somewhere first if one is given, observed as `nav.observe` does; the world itself untouched |
| Budgets and tiers | `nav.where` within a `budget` of bytes (walkers left out from the last, `more` says how many); `Options`: `keep` steps of history, a checkpoint `every` so many, `most_steps` a request |
| Not here | **Causes** as a record — which walker made another step aside, why a path was planned again — and **source**: `capabilities` says so and the contract suite reports them unsupported. A walker does not see a 3D rigid world's moving bodies (only obstacles placed here) |

## What it is

```cpp
namespace nav = xengine::nav;
nav::Ground ground{.points = level_points, .corners = level_triangles};          // three corners a triangle
auto world = nav::World::build(ground, {.radius = 0.4f, .height = 1.8f, .climb = 0.4f, .slope = 0.785f});
const auto way = world->path({-5, 0, -5}, {5, 0, -5});        // corners, length, reaches or not, polygons crossed
const auto walker = *world->add({.at = {-5, 0, -6}});          // a walker of the crowd
world->target(walker, {5, 0, -5});
world->step();                                                  // 1/60 s: every walker steered, avoided, moved
const auto door = *world->place(nav::Obstacle::box({0, 0, 8}, {1, 1, 1}));  // the tiles under it built again
const auto saved = world->save();                               // the mesh's tiles, obstacles, links, every walker
auto branch = world->branch();
```

- **The mesh is built in tiles** from the ground's triangles for one size of walker (its radius, height, the highest step it climbs, the steepest slope it walks); an obstacle (a cylinder or a box) or a link added later builds only the tiles it touches again. A tile is a function of the triangles, the numbers, the obstacles and links over it: the same bytes every time.
- **Paths**: the polygons from one place to another (A* over the mesh), the corners a walker turns at, the nearest place on the mesh, a straight walk along it until something stops it.
- **Crowds**: walkers with a radius, a speed and an acceleration, each sent to a target, steered along their corridors, keeping apart and avoiding each other (Detour's crowd); across a link they are carried from one end to the other.
- **The world is a value**: `save` writes everything a step depends on, and `restore`, `from` and `branch` step on to the same bits as the run never interrupted — with walkers half across a link (`nav_test` restores at the first frame one is). Detour's own crowd keeps paths half-planned in a queue and links half-crossed where nothing can read them, and has no image; so here a path is planned whole when it is asked for and wherever dtCrowd would plan again (its own checks, made before its step), a walker's local boundary is kept as the polygon and centre it was found from, and a link crossed is carried with dtCrowd's arithmetic.

## Determinism

The same ground, numbers, obstacles, links and inputs give the same mesh bytes and the same walkers' places: the wrap and this package are built with no contraction of `a*b+c` into an FMA and no fast math. Measured: the crowd scene's hash after 1500 steps is the same in the dev and release profiles on Linux x86-64 (GCC 16), and pinned in the tests; a world restored or branched from bytes steps on to it. The wrap's CI pins Detour's own mesh bytes and crowd on Linux (x86-64, arm64; GCC and LLVM), macOS, Windows and wasm32 — the same on every row, the crowd's `cosf` and `sinf` (from the platform's C library) included; this package's pin is checked where the workspace's CI runs it.

## Against the strongest

The strongest is Recast & Detour itself — the industry's navigation mesh, under Unity's and Unreal's — so the mesh and its paths are its, polygon for polygon (par). Ahead: the crowd is a value — an image, a restore and a branch to the same bits, which Detour's crowd does not have — and an agent's subject with a guided task. Cost: the crowd step is 1.8% over Detour's own (64 walkers, 600 steps, release, x86-64: 123.3 ms against 121.1; `simulation/nav-crowd-64`). Behind: an obstacle rebuilds the tiles it touches with Recast (0.44 ms for one in the doorway here, release) where DetourTileCache rebuilds them from compressed layers, cheaper for many obstacles moving every second; the tile cache is the step to take when a game needs that.

## Running it

The tests are in [cts](cts) (`xengine.nav.cts`: the world contract's adapter and the scenes the tests and `tests/bench` walk — a room with a wall, a doorway, a platform and a link up onto it):

```sh
mcpp test -p xengine.nav.cts   # built, asked, walked; obstacles and links; restored mid-crossing; the world contract; the agent contract
```

`simulation/nav-crowd-64` in `tests/bench` steps 64 walkers on a `World` and on Detour's crowd called directly on the same mesh and walkers (`nav::Direct`), taking turns: 1.8% over (release, x86-64; a budget holds it under 10%).
