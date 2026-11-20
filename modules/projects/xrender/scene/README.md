# xrender.scene

What there is to draw in three dimensions, and what one view of it sees: instances and lights under stable ids, every edit on a change list, and the extraction a renderer's passes are given — the visible instances, sorted and batched, the lights that reach the view. It draws nothing and owns no device (the foundation plan's §5.1, M6.3; D-164).

```cpp
import xrender.scene;
namespace scene = xrender::scene;

scene::Scene world;
world.add({.id = 17, .mesh = crate, .material = wood, .world = math::translation({2, 0, -5}), .bounds = crate_bounds});
world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .toward = {0, -1, -0.3f}});

scene::Extracted seen;
scene::extract(world, {.view = view, .projection = projection}, seen);   // seen.draws, seen.batches, seen.lights
world.move(17, math::translation({3, 0, -5}));
for (const scene::Change& change : world.changes()) { /* a mirror follows: moved 17 */ }
world.settle();                                                          // the frame is over: revision + 1
```

| | |
|---|---|
| **What an instance says** | its mesh and material (their owner's numbers), where it is, its bounds, its layers, whether it is hidden, whether it `casts` a shadow (a lamp's globe does not stand in its own light) |
| **Stable ids** | An instance and a light are their id — a number their maker gives (a file's place, a path's hash), the same in every run. Nothing is addressed by where it sits in an array; `instances()` is in id order whatever order they were added in |
| **The change list** | `add`, `remove`, `move`, `set` each leave a `Change` (what, which id), in the order made, until `settle()`. Another representation of the scene — a tree for culling, an agent's projection, later a set of acceleration structures — is built once and then follows the list (the plan's §9.4: left so that those need no second extraction). `Spatial` is the one that is here: a tree of the instances' boxes in the world. Where things only moved it keeps its shape and refits its boxes (100 of 10,000 moved: 0.65 ms in a debug build, against 22 ms made again); it is made again at once when something came or went. After 64 refits a new tree is made beside it — `Spatial::rebuild_things` (4,096) things looked at each time it follows, in the room of the tree last replaced — and takes its place when whole: no frame pays for the whole tree (10,000 things, 100 moved a frame, a debug build: the largest frame 2.8 ms where it was 34 ms, p99 1.95 ms where it was 31) |
| **The extraction** | `extract(scene, view, out)`: every instance not hidden, on one of the view's layers and not wholly outside its frustum is a `Draw`; draws are sorted by material, then mesh, then nearest first, then id, and runs of one mesh and one material are a `Batch` (what an instanced draw is made of). Lights: a sun always, a point or a spot where its reach touches the frustum. The same scene and view give the same bytes; `out` is reused and a steady frame allocates nothing |
| **Two paths, one answer** | `extract(scene, view, out)` looks at every instance (the reference); `extract(scene, spatial, view, out)` walks the tree and looks only at what a box of it lets through. They give the same draws (`tests/scene_test.cpp` holds them to it over thousands of views) |

## Agent surface

The scene is part of the frame's subject ([xrender.frame](../frame)): a pixel is traced back to an object's id there, and that id is an instance's here. What this package gives an agent's question, as plain functions a subject calls:

| | |
|---|---|
| State and addresses | An instance or a light by its stable id; `instances()`, `lights()` in id order; `revision()` counts the frames settled |
| Observation | `extract`: what a view sees, as ids — no pixels needed to know that the crate is in view. `world_bounds(id)`: where a thing is |
| Causes | `why_unseen(scene, view, id)`: seen, or the first reason it is not — absent, hidden, on none of the view's layers, outside the frustum. `changes()`: what was done to the scene this frame, in order |
| Control | `add`, `remove`, `move`, `set`: each is refused with its reason (a duplicate id, an id that is not there, a box that holds nothing, a matrix that is not finite) and leaves the scene as it was |
| Time | A scene has no clock: `revision()` moves when its owner settles a frame. What moves things is their owner's subject |
| Not here | Occlusion (what is in the frustum but behind a wall is still a draw: the frame's id buffer says what reached a pixel); who may edit (the owner's subject judges); pixels |

```sh
mcpp test -p xrender.scene
```
