# meng.field

The air and what moves it, as one more carrier of [meng.matter](../matter) (the [M6.5 design](../../../../../.agents/docs/2026-11-15-m6.5-emergence-3d-design.md)'s T5.2; the foundation plan's §6.4). A sparse lattice of coarse cells, made in blocks of 8³ only where something is: each cell is an instance of matter — the air, its temperature, wetness, charge — and the standard laws run between neighbouring cells as they run on a fine lattice, on bodies and on particles. What the carrier does itself is move things: the wind. Warm air rises (buoyancy from how much warmer a cell is than the air about), the wind relaxes toward the far wind, it carries itself and the heat along (semi-Lagrangian advection), and a fixed number of pressure iterations keeps it from piling up. A fire under it makes an updraft; a wind blows a fire's heat downwind. Nothing here names a fire or a wind's effect.

```cpp
import meng.field;
namespace field = meng::field;

auto air = field::Field::make(program, {.air = *program.material("air"), .wind = {2, 0, 0}}).value();
air.cover({0, 2, 0}, 6);                                    // cells about a place
air.set(*air.cell_at({0, 0.5f, 0}), temperature, hot);      // a quantity applied
air.step(guests);                                           // the laws across the cells and the guests in them, then the wind
air.wind_at({0, 3, 0});                                     // what a body or a particle there is pushed by
```

| | |
|---|---|
| Cells | `cell` m across, made in blocks of 8³ by `cover`; a new one is the air, still, at `air_kelvin` |
| Laws | The program's, between each cell and its six neighbours, and between each guest and the cell it is in — one matter step for all |
| Wind | Buoyancy (`buoyancy` m/s² a kelvin above the air's), a return toward the far `wind` (`relax` a second), self-advection, `projection` pressure iterations; then the temperature advected by it |
| Guests | Another carrier's instances for a step (`Guests`: state, pairs among them, where each is): bodies (`meng.dynamics.objects`), particles |
| Determinism | Cells in the order their blocks were made, pairs in a fixed order, the wind's floats by + − × ÷ and floor with no contraction, the kernel's reference step: the same bits everywhere; a copy is a branch |

## Agent surface

The air is a `Subject` of the agent kernel ([mlibs.agent](../../../../base/agent)), module `meng.field.agent` — the package's feature `agent` (`meng.field = { path = …, features = ["agent"] }`), so that the carrier taken alone stays the small tool it is. Its operations are a catalog, [air.ops.xdef.ts](air.ops.xdef.ts) (the table `src/agent/catalog.cppm` and the listings `src/agent/air.ops.json`, `.md` are `ops-gen`'s); how to use them, in order, is [skill.omui.md](skill.omui.md). Every operation is decided by openas first.

| | |
|---|---|
| Subject and addresses | `air`, interface `meng.field.air@0.1`. A place is `[x, y, z]` in metres, its cell the one there; a frame is a step played |
| State | The `Field` on `mlibs.simulation`'s timeline, copied whole (a copy is a branch); the hash is the field's `state_hash`, bit for bit |
| Observation | `air.observe`: cells, parked blocks, the strongest wind, each channel's least, most (and where), total — within a budget, channels left out from the last; `air.at`: one place's cell, channels, kelvin, wind |
| Control | `air.set`: a quantity at a place's cell from the next step, once or held (a fire); `air.step` |
| Time | `air.seek`: any frame of the history kept, the same bits; what is held is let go |
| Preview | `air.branch`: some steps ahead with one more quantity applied, the air untouched |
| Causes | `air.explain`: for a place's cell after the last step, each law that held, each flow in and out by its law and the neighbour's place, the channels after |
| Not yet | the source of what it puts out back to what made it, across carriers (`world.explain`, with the world's subject); undo apart from a frame returned to |

[cts](cts) holds it to the agent contract (`mlibs.agent.cts`) and lights a fire through the session.

## Against the strongest

The open-world engines move air for effects only (a wind direction for grass and cloth, a fire's spread as a script or a cellular rule of its own). Here the air is matter under the same laws as everything else, so heat, wetness and charge go through it to and from bodies, and the wind is made by heat rather than set. Behind: a coarse grid (1 m), no obstacles in the wind yet (bodies do not block it), heat capacity by volume not yet (a cell is one instance, as a body is).

## Experimental (release 0.1, D-199)

- **Target**: the ambient carrier of matter — air, heat, wetness and wind — giving physical magnitudes under the same laws as the other carriers, on the CPU reference and the GPU.
- **Now**: the sparse lattice of 8³ blocks, buoyancy, semi-Lagrangian advection, a fixed count of pressure iterations; Glade the one work that uses it, beside meng.dynamics' `rigid3-matter`.
- **Gap**: no radiation (R11b's P1 channel); magnitudes not checked against hand physics; no GPU path.
- **Limits**: a coarse grid (1 m cells); bodies do not block the wind; a cell is one instance of matter (its heat capacity not by its volume); the CPU only.
- **Graduation to preview**: R11's magnitudes within tolerance, a contract suite run on every tier-1 row, a second consumer.
