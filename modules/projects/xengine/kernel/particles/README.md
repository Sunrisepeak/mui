# xengine.particles

Things that fly in 3D — sparks, embers, drops, grit — as one of matter's four carriers (D-166: lattice cells, bodies, particles, ambient fields; the same laws on all). A particle flies under gravity and drag, meets the world's triangles first along its path and goes on reflected by its material's `restitution` and `friction`, ages and is gone when its life is over; and it is an instance of matter, beside those within `near` of it and beside the air about them (a bath at `ambient_kelvin`), so a spark cools, two share their heat, wood near a hot one lights — by the definition's laws, none here.

```cpp
import xengine.particles;
namespace particles = xengine::particles;

auto cloud = particles::Cloud::make(program, {.ambient = *program->material("air"), .ambient_kelvin = 293});
cloud->emit(sparks);                       // where, how fast, how big, how long, of what, how hot
cloud->step(world);                        // a step: fly, meet the triangles, one tick of matter
for (const particles::Landed& one : cloud->landed()) hear(one);   // what struck something, how fast, where
particles::Cloud branch = *cloud;          // a copy is a branch
```

| | |
|---|---|
| **Flight** | Speed first (gravity, then drag), then place: semi-implicit, the same bits every run. Where the step's path meets a surface (`Surface`: a mesh, its tree from `mlibs.geom`'s `bvh_of`, its place) the nearest meeting stops it there, a little off the surface, its speed into the surface kept by `restitution`, along it less by `friction` — and it is a `Landed`: the particle, its material, its speed into the surface, where and which way the surface faced |
| **Matter** | Each step, after flight, one tick of the program: the particles and the air (one instance, set to `ambient` at `ambient_kelvin` each step), each particle beside those within `near` (found by cells of that size, taken in order) and beside the air (`ambient_weight`). `events()` is what the laws did, `value(channel, particle)` a channel in its unit |
| **Colour** | A cloud's state is channel-major like a lattice's, so the world's look colours it by the same rule: `mlibs::math::color::shade_run(look.shade(), cloud.state(), cloud.size(), 0, pixels, light)` — an iron spark at 1500 K glows, after 10 s in the air it gives less light; a new material is charred, wet or aglow like the rest, with no particle code |
| **Order and agreement** | Particles keep the order they were emitted in (one gone leaves the rest as they were); `fingerprint()` hashes every number; two clouds of the same spawns agree, and so does a copy taken part way |

## Agent surface

A subject of its own, `particles` (the feature `agent`, module `xengine.particles.agent`; catalog [particles.ops.omui.ts](particles.ops.omui.ts), order of use [skill.omui.md](skill.omui.md)): `observe` (the frame, the fingerprint, how many by material, what struck the world fastest first, within a budget), `list`, `near`, `emit` (a material by name, how many, a spread the same every run), `step`, `seek` (a copy of the cloud every 60 steps and the emissions since, played again — bit for bit), `branch` (what would be, the cloud untouched). Under the agent contract's suite in `xengine.matter.cts` beside the lattice's subjects: everything it says it has passes; causes and the source of what it puts out are said unsupported. The cloud itself is a value: `at`, `velocity`, `ages`, `lives`, `ids`, `state`, `landed`, `events`, `fingerprint`; a branch is a copy (D-169). Drawn by [xrender.particle](../../../xrender/particle) (a particle's place, size and colour), which also says which one is at a point (`pick`) — the colour from the state by the appearance recipe (D-168).

## Against the strongest

Engines' particle systems (Unreal's Niagara, Unity's VFX Graph, Godot's GPUParticles3D) fly particles with collision against depth or signed-distance fields and modules that change colour and size over life by curves. Here a particle is matter: what happens to it is the world's laws (a spark cools by heat flow into the air, lights wood it lands near), not a curve per effect; collision is against the true triangles; and every run is the same bits, a copy a branch. Behind: one thread on the CPU (the device is the next rung, from the same numbers), no curl noise or force fields yet.
