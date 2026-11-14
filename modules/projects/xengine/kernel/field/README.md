# xengine.field

The air and what moves it, as one more carrier of [xengine.matter](../matter) (the [M6.5 design](../../../../../.agents/docs/2026-11-15-m6.5-emergence-3d-design.md)'s T5.2; the foundation plan's §6.4). A sparse lattice of coarse cells, made in blocks of 8³ only where something is: each cell is an instance of matter — the air, its temperature, wetness, charge — and the standard laws run between neighbouring cells as they run on a fine lattice, on bodies and on particles. What the carrier does itself is move things: the wind. Warm air rises (buoyancy from how much warmer a cell is than the air about), the wind relaxes toward the far wind, it carries itself and the heat along (semi-Lagrangian advection), and a fixed number of pressure iterations keeps it from piling up. A fire under it makes an updraft; a wind blows a fire's heat downwind. Nothing here names a fire or a wind's effect.

```cpp
import xengine.field;
namespace field = xengine::field;

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
| Guests | Another carrier's instances for a step (`Guests`: state, pairs among them, where each is): bodies (`xengine.dynamics.objects`), particles |
| Determinism | Cells in the order their blocks were made, pairs in a fixed order, the wind's floats by + − × ÷ and floor with no contraction, the kernel's reference step: the same bits everywhere; a copy is a branch |

## Agent surface

What it gives an agent now: the state (`get`, `kelvin_at`, `wind_at`), why an instance is what it is after a step (`why`: every law judged, every flow named by the instance it came from — a cell, a body, a particle), the fingerprint (`state_hash`), and a branch (a copy). Its own subject — `observe`, `explain` across carriers, `branch`, `apply` — comes with T5.5 (`world.explain` across carriers), beside the lattice's and the particles' under the agent contract's suite.

## Against the strongest

The open-world engines move air for effects only (a wind direction for grass and cloth, a fire's spread as a script or a cellular rule of its own). Here the air is matter under the same laws as everything else, so heat, wetness and charge go through it to and from bodies, and the wind is made by heat rather than set. Behind: a coarse grid (1 m), no obstacles in the wind yet (bodies do not block it), heat capacity by volume not yet (a cell is one instance, as a body is).
