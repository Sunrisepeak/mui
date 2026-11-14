# xrender.particle

Particles drawn: sparks, embers, smoke, drops — squares facing the eye laid over the light a mesh pass drew, added (fire, glints) or laid over by how much they hide (smoke, dust), faded where they near what is behind them. This is the CPU reference, the same bits everywhere; a device's path is the same contract's other target. What moves particles is not here: a carrier steps them (xengine), and this draws what it is given.

```cpp
import xrender.particle;
namespace particle = xrender::particle;

const std::array<particle::Batch, 2> batches{
    particle::Batch{.particles = sparks, .blend = particle::Blend::add, .soft = 0.05f, .id = 40},
    particle::Batch{.particles = smoke, .blend = particle::Blend::over, .soft = 0.3f, .sprite = &puff, .id = 41}};
mesh_pass.draw(world, seen, view, assets, frame);     // the meshes: their light and depth
particle::draw(batches, view, frame);                 // over the light, faded at the surfaces
const auto spark = particle::pick(batches, view, x, y, frame.width, frame.height);  // batch 40, particle 7
```

| | |
|---|---|
| **A particle** | Its middle in the world, its width, its light (linear, not held to 1: a spark brighter than white glows through `Glow`) and how much it hides, a turn about the way to the eye, its carrier's number for it |
| **Shape** | A round spot, whole to half its radius and smooth to nothing at its edge; or a picture (`Batch::sprite`, an `xrender.mesh` `Texture`): its light times the particle's, its cover how much is there, read at the size the particle is on the screen |
| **Where a pixel takes it** | The eye's ray through the pixel's middle meets the particle's square (in the plane facing the eye at its middle): inside, the shape there. Exact for any projection; a turn turns the shape and the square |
| **Order** | Every particle of every batch, the farthest first; at one distance, in the batches' order. `add` puts its light on; `over` lays it on by what it hides (the light under it and `Targets::clear` less by that share) — the same picture whatever order a carrier gives them in |
| **Soft at surfaces** | Within `soft` of what the frame's depth says is behind it, a particle fades in proportion; behind a surface it is not there; `soft` 0 cuts it where it goes in |
| **Not written** | Depth, normal, velocity, who drew a pixel: particles are not a surface; the meshes keep those |

## Agent surface

Part of the frame's subject ([xrender.frame](../frame)); what this package gives it:

| | |
|---|---|
| Source | `pick(batches, view, x, y, width, height)`: the nearest particle at a point whose shape is there — its batch's id (an emitter's), its own number and its distance — with no frame drawn |
| Observation | `Drawn` from `draw`: particles looked at, those behind the eye or off the frame, pixels reached |
| Not here | Control and time: the carrier's (it steps them; its subject is where an agent changes them) |

## Against the strongest

The engines' particle renderers (Unreal's Niagara sprite renderer, Unity's VFX Graph, Godot's GPUParticles3D) draw camera-facing quads on the device, soft by the depth buffer, sorted per system. Here: the same model, and a CPU reference they do not have — each pixel's answer is the same bits on every machine, so a device's path is held to it pixel by pixel, and an agent can ask which particle is at a point (`pick`) without a frame. Its device path is [gpu](gpu) (`xrender.particle.gpu`), held to these pixels by its suite. Behind: a picture's particles on a device, no light falling on a particle from the scene's lamps (a particle gives its own), no sorting cost measured.

```sh
mcpp test -p xrender.particle
```
