# A world in three dimensions

How a 3D game is made with omui's engines, read from [Quay](../../../showcase/quay) (preview): a watchkeeper on a harbour at night lights a beacon while rusted wardens patrol. The game is a pure function of a state and a step's actions; the picture, the sound and the window only read the state.

## Bodies and a crowd

The harbour is a 3D rigid world (`meng.dynamics`' rigid3: Jolt under the engine's contract): the containers fixed, the crates moving, the watchkeeper a character. The containers' triangles also make the mesh rays and sounds meet; steel lets 0.15 of a sound by and dulls it above 800 Hz.

<!-- code: showcase/quay/src/sim/sim.cpp#bodies -->
```cpp
auto bodies = rigid3::World::make();
if (!bodies) return state;
for (const Box& box : level.boxes) {
    if (box.crate) {
        const auto crate = bodies->add({.kind = rigid3::Kind::moving, .at = box.at}, rigid3::Shape::box(box.half), {.density = 300, .friction = 0.7f});
        if (crate) state.crates.push_back(*crate);
        continue;
    }
    (void)bodies->add({.kind = rigid3::Kind::fixed, .at = box.at}, rigid3::Shape::box(box.half));
    (void)geom::append(statics->mesh, geom::box(box.half), math::translation(box.at));
}
statics->tree = geom::bvh_of(statics->mesh);
// Steel lets a tenth of a sound by, and dulls it above 800 Hz.
statics->walls.push_back({.mesh = &statics->mesh, .tree = &statics->tree, .place = math::Mat4{}, .through = 0.15f, .bright = 800});
const auto keeper = bodies->add(rigid3::CharacterDef{.at = level.start});
```

The wardens walk a navigation mesh built from the same triangles (`meng.nav`: Detour's crowd), each given its round:

<!-- code: showcase/quay/src/sim/sim.cpp#crowd -->
```cpp
auto ground = nav::World::build({.points = statics->mesh.positions, .corners = statics->mesh.indices}, {.radius = warden_radius, .height = warden_height});
if (!ground) return state;
for (const Patrol& round : level.wardens) {
    const auto walker = ground->add({.at = round.from, .radius = warden_radius, .height = warden_height, .speed = 2.6f});
    if (!walker) continue;
    (void)ground->target(*walker, round.to);
    state.wardens.push_back({.walker = *walker, .round = round, .at = round.from});
}
```

## A copy is a branch

Both native worlds are held by `mlibs::simulation::Branching`, so the whole state is a value: copying it is a branch an agent plays ahead on, and the runtime's `world.seek` goes back to any step with the same bits.

<!-- code: showcase/quay/src/sim/sim.cpp#branching -->
```cpp
state.bodies = mlibs::simulation::Branching<rigid3::World>(std::move(*bodies));
state.ground = mlibs::simulation::Branching<nav::World>(std::move(*ground));
```

## What a person hears and sees

The person's view (`quay::observe`) never says where anything is. Sounds reach the ears by `meng.audio.space`'s `reach` — the rule the speakers play by — through the containers' steel; what is too quiet is not heard:

<!-- code: showcase/quay/src/headless/headless.cpp#hearing -->
```cpp
const space::Listener ears{.at = eye, .forward = {static_cast<float>(det::sin(yaw)), 0, static_cast<float>(-det::cos(yaw))}, .up = {0, 1, 0}};
bool first = true;
for (const Noise& noise : state.recent) {
    if (noise.by == 0) continue;
    const space::Reach reached = space::reach(ears, noise.at, state.statics->walls);
    const float level = noise.loudness + reached.level;
    if (level < heard_from) continue;
```

## Drawn by the CPU reference, then on a device

`quay::Picture` draws a frame from the state with `xrender.mesh`, the CPU reference: the same pixels on every machine. Rain is `xrender.particle`; the glow is the frame's. `OnDevice` draws the same frame on a device and is held to these pixels.

<!-- code: showcase/quay/src/picture/picture.cpp#draw -->
```cpp
prepare(state);
// The reference's shadows are traced through each mesh's tree: the bent bodies' made again (a device's maps need none).
if (frame.shadows)
    for (std::size_t warden = 0; warden < bodies(); ++warden) grown_[body_mesh(warden)] = geom::bvh_of(made_[body_mesh(warden)].mesh);
targets_.resize(frame.width, frame.height);
mesh::Options options = options_;
options.shadows = frame.shadows;
pass_.draw(world_, seen_, view_, assets(frame.shadows), targets_, options);
rained_ = xrender::particle::draw(batches_, view_, targets_);
shown_ = targets_.colour;
bright_ = frame.glow ? glow_.show(targets_.light, frame.width, frame.height, bloom(frame.width), shown_).bright : 0;
return targets_;
```

## Run it

```sh
mcpp test -p omui.showcase.quay           # the harbour, the person's view, the agent playing as a person, the picture, the sound
mcpp test -p omui.showcase.quay.window    # played in a window; its recording the same bits with no window
mcpp test -p omui.showcase.quay.play      # on this machine's device, held to the CPU reference
```

## When something goes wrong

- **No device**: `OMUI_REQUIRE_GPU=1` makes a missing device a failure; on Linux without a GPU, `omui-devtools gpu` installs lavapipe, the software Vulkan CI uses. A device's results are marked `software` in the [platform table](../platforms.md), not `real`.
- **A replay differs**: a recording names its level and replays bit for bit; a different checksum means the state's step changed — `quay::checksum(state)` is one hash of a step's state, so comparing it step by step finds the first step where two runs part.
- **The Web frame**: the page's WebGPU frame time is held to 16.7 ms only on a real GPU; on a software adapter it is printed, not held ([platforms](../platforms.md), `frame-budget-weak`).
