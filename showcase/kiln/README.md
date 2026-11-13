# Kiln

Mui the pixel cat, with a wand of fire, water and bombs, in caverns where everything is matter: the world is the standard definition of matter on a lattice of 0.1 m cells, with rigid bodies made of its cells and cells in flight ([xengine.cellular](../../modules/projects/xengine/kernel/cellular)'s `MatterWorld`). Nothing in it is scripted: a barricade of wood burns because fire heats it past its ignition point, a crate floats because wood is lighter than water, a wall gives way to a blast, water poured on lava sets it to stone. M6.2's vertical slice ([the plan's](../../.agents/docs/2026-11-12-foundation-omui-xengine-emergent-world-design.md) T2.6–T2.7).

**State of it**: the simulation (below) is done and tested; the game as `xengine.runtime` plays it (an agent playing it, branching ahead), its picture and a window to play it in are next.

| Package | What |
|---|---|
| [sim](sim) | The simulation: Mui (walks, climbs a step of a cell, jumps, swims and runs out of breath, is hurt by heat and fire), the wand (bolts of fire, water, bombs), the first cavern, the exit; a pure function of the state and the frame's actions, its state a value (a copy is a branch) |

The first cavern: Mui starts on a ledge; a barricade of wood bars the way (fire); a pool with a crate afloat (jump, or swim before her breath runs out); a stone wall to the ceiling (a bomb; a keg of gunpowder by it is a hazard, not a key); a pit of lava (water sets it to stone); the exit beyond.

```sh
mcpp test -p omui.showcase.kiln.sim     # stands, burns the barricade open, swims, holes the wall, cools the lava; the same frames bit for bit
```
