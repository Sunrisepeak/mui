# xengine.matter

What things are made of, the state each instance carries, and laws that read properties and never a material's name — so a material that only says its properties takes part in everything (D-165).

Physics moves things; this is what they are and what they become. Fire does not know wood: it raises a temperature, and whatever has fuel, an ignition point below that temperature and oxygen beside it burns.

```ts
// world.matter.omui.ts — data, evaluated to the JSON snapshot the kernel reads (D-139)
import { defineMatter } from "omui/define";
export default defineMatter({
  materials: {
    air:   { phase: "gas", density: 1.2, oxygen: 1 },
    wood:  { density: 600, ignition: 570, fuel: 400, heat: 80, burnt: "ash" },
    ash:   { phase: "granular", density: 400 },
    water: { phase: "liquid", density: 1000, melting: 273, boiling: 373, boiled: "steam", frozen: "ice", moist: 64, porous: 8 },
    // …
  },
  laws: [
    { transfer: "temperature", by: "thermal" },                                             // heat flows, by how well both ends conduct
    { when: "wetness > 0", raise: { ignition: "wetness * 40" } },                           // wet is harder to light
    { when: "temperature >= ignition && burned < fuel && near.oxygen > 0",
      add: { burned: "burn_rate" }, flag: "burning" },                                      // burning
    { when: "burning > 0 && temperature < flame", add: { temperature: "heat" } },           // as hot as its flame, no hotter
    { when: "phase == liquid && boiling > 0 && temperature >= boiling", become: "boiled" }, // it becomes *its* gas
  ],
});
```

```cpp
import xengine.matter.json;   // the kernel (xengine.matter) and a definition from its JSON snapshot
namespace matter = xengine::matter;

auto program = matter::prepare_document(snapshot, "world.matter.omui.json");   // the plan, prepared once; problems by JSON pointer
// or matter::standard(): the standard definition, carried in the module
std::vector<std::uint32_t> state = program->fresh(instances, *program->material("air")), next = state;
program->place(state, instances, 7, *program->material("wood"));
matter::Workspace workspace;
std::vector<matter::Event> events;
if (auto stepped = program->step({instances, pairs}, state, next, workspace, &events)) state.swap(next);   // one tick
```

## The model

| | |
|---|---|
| **Columns** | A property every material has a value of: a number with a unit and a range, a phase, or another material (what this one becomes). The standard ones: mechanics (`density`, `friction`, `restitution`, `compliance`, `shape_stiffness`), heat (`thermal`, `ignition`, `melting`, `boiling`, `warmth`, `starts_at`), burning (`fuel`, `burn_rate`, `heat`, `flame`, `oxygen`), water (`moist`, `porous`), electricity (`conduct`), `phase`, and what it becomes (`frozen`, `melted`, `boiled`, `condensed`, `burnt`). A definition adds its own |
| **Materials** | A row: what it does not say is the column's default. `Program::value` is what the laws read, `number` what was written |
| **Channels** | What each instance carries, unsigned 32-bit: `material`, `temperature`, `wetness`, `charge`, `burned`, `burning`, `damage`; a definition adds its own. A channel may start from a property (`temperature` from `starts_at`: lava is placed hot), reset when the instance becomes something else, and name the events of leaving 0 and returning to it (`burning`: `ignite`, `go_out`) |
| **Laws** | Three kinds. A **transfer**: a channel flows between neighbours, from more to less, by the lesser of their two values of a property — the same amount counted at both ends, so what there is of it stays. A **reaction**: where a condition holds, channels are added to, taken from or set, a flag is raised; with `become`, a **conversion** — the instance turns into the material *its own* property names (`boiled`, `frozen`, `burnt`). And an **adjustment**: a property as the other laws read it, raised or lowered by state |
| **Conditions and amounts** | `temperature >= ignition && burned < fuel && near.oxygen > 0`: comparisons joined by `&&`, each between sums of channels, properties, `near.` sums over the neighbours, and whole numbers; a term × a whole number, / a power of two. Written in units — `temperature >= 373` is kelvin — and brought to whole numbers when prepared |
| **Carrier** | Whoever has instances and says which are beside which, with a weight: a lattice's cells and neighbourhoods, bodies and their contacts, particles and their near ones, a sample of an ambient field. The same laws on all of them |
| **Program** | The definition prepared once into an immutable plan (the preparation contract, `mlibs.cts:prepare`): names resolved, values checked against their columns, conditions parsed; refused with a code, a JSON pointer and what to write instead (`matter.name at /laws/13/when: `temprature` is no channel, property or phase (did you mean `temperature`?)`) |

**No law names a material.** `material == wood` in a condition, or `become: "steam"`, is refused (`matter.names-material`): a thing becomes what its own property says, so each material says what it turns into and the law only says when. A special case that must name one says `recipe: true`; `Program::recipes()` counts them — the measure that should stay near none.

**Whole numbers, in units.** Every value a law reads is an unsigned 32-bit integer and every sum saturates, so a step is the same bits on every machine. A column or a channel has a number of bits after the point (`fraction`): temperature is stored in 256ths of a kelvin, because a flow is a share of a difference and a difference of 47 K, in whole kelvin and at a 64th a step, would not flow at all. Laws are still written in kelvin: preparing brings every term of a comparison, and every amount, to its target's unit.

## One step

Every law reads the step's input; nothing a law writes is seen by another in the same step. In order ([CONTRACT.md](CONTRACT.md) has the arithmetic, which a carrier's own path is held to):

1. **near** — what some law reads of the neighbours, summed by weight;
2. **flows** — each transfer, pair by pair, the same amount lost at one end and gained at the other;
3. **laws** — per instance: its properties as its state leaves them (adjustments), then every reaction and conversion that holds;
4. **commit** — per channel: the first `set`, plus what is added, less what is taken, less what flowed out, plus what flowed in; a conversion changes the material and resets the channels that say so;
5. **events** — an instance became another material (which law, from what, to what); a channel that names them left 0 or returned to it.

A transfer whose neighbours could together take more than an instance has is refused before anything is written (`matter.unstable`), not clamped: clamping would make what leaves one end differ from what arrives at the other.

## What the tests show

`mcpp test -p xengine.matter` — the standard laws ([standard.matter.omui.ts](standard.matter.omui.ts): 14 laws, 16 materials, no law naming one) on rows and small grids of instances:

- heat flows along a row and its total stays, step after step; iron carries it further than stone;
- a strip of wood lit at one end burns piece by piece (caught at steps 1, 61, 103, 142, 181, 220, 256, 281 — each from the last), every piece ash after 681; sealed from air nothing burns. What burns heats up to its flame's temperature (`flame`, 1100 K unless a material says) and no further: a fire is as hot as a fire is however closed the room, and the stone under a wood fire stays stone;
- wet wood does not light at 600 K and dry wood does; wood beside water becomes wet, the nearest wettest; in the heat it dries;
- water boils to steam, steam condenses, water freezes, ice melts — each material saying what it becomes;
- lava beside water: the water boils first, then the lava sets as stone;
- wood at the far end of an iron bar from a heat source catches at step 144, of a stone bar at 421; charge goes through iron and water and stops at wood;
- **alcohol and its vapour are added as two rows of properties and no law is changed**: it boils at its own point, its vapour condenses, catches beside a fire at step 14 where wood catches at 47, and burns out to smoke — 6 interactions for 2 rows;
- the same state whatever the order of the pairs; the arithmetic of one step against numbers worked by hand, saturating at both ends; a definition refused 23 ways, each by its code, pointer and message; JSON problems at their line and column.

**A carrier is held to this step.** [cts](cts) (`xengine.matter.cts`, contract `xengine.matter.carrier@1`) steps eleven small worlds by the reference and by a carrier from the same state and compares every value after every step — heat, fire, wet and dry, the changes of phase, charge, soups of every material (wrapped edges too, and a grid two wide), instances that are no material, values at the ends of 32 bits, a material added as rows, and a definition of its own using every kind of read and write. A carrier one pair wrong fails it; the pairs in another order do not. The lattice passes it whole, in small tiles and run sparsely (`mcpp test -p xengine.matter.cts`), and the same file has what the lattice adds — cells that move — on the standard definition.

`tests/prepare_test.cpp` is the preparation contract on it (12 cases). The reference step on 4096 instances each beside four is 2.3 ms in a debug build with nothing allocated (`omui-bench --filter simulation/matter`); it is the reference, not the path a large world runs.

## Where it sits

Base layer. The kernel — module `xengine.matter`: definitions, the plan, the reference step — takes the standard library and `mlibs.utils` (errors, names, trace) and nothing else: `xengine.matter = { path = …, default-features = false }` is what a carrier's adapter takes (`xengine.cellular`'s and `xengine.dynamics`' feature `matter`). The feature `json` (on by default; module `xengine.matter.json`) adds a definition from its JSON snapshot with `mlibs.json`, and `standard()`: the standard definition carried in the module (`standard_snapshot`, the committed JSON embedded; the generator's test keeps the three — data module, snapshot, embedded text — the same). It knows no lattice, no body and no renderer. A carrier adapts on its own side: `xengine.cellular`'s feature `matter` lowers a program to the lattice's (`cellular::lower`: the materials' table, transfers by a column, reactions), where cells also move by `density` and `phase`; `xengine.dynamics`' feature `matter` reads the mechanical columns (`params_of(table, material, area)`: a soft body's mass, friction, restitution, compliance and shape stiffness — the table it had of its own is this one now) and will give contacts as pairs. What a state looks and sounds like is a recipe's, above (D-168).

Trace: category `simulation`, spans `simulation.matter.prepare` and `simulation.matter.step`, counters `simulation.matter.held` and `.became`.

## Agent surface

A definition and its plan hold no state of their own: the state a program steps is the carrier's, and an agent reaches it through the carrier's subject (the lattice's `field`: regions as numbers, placing, stepping, seeking, branching). What this package gives an agent directly:

| | |
|---|---|
| What a world is made of | `Program::describe()`: columns with units and ranges, channels, every material with what differs from the defaults, every law with its kind, its condition as written, what it changes — as JSON |
| What may be written | `omui/define`'s `defineMatter` types (the TS stubs), and the reader's problems: file, line, column, JSON pointer, and the nearest name |
| Preview | `Program::prepare` and `prepare_document` are pure: a changed definition is prepared and stepped on a copy of the state before it replaces anything |
| Not here yet | Its own catalog — materials by region, a quantity applied, **why** an instance is in the state it is in (which law, from which neighbours) — comes with the record of causes written at the commit (the plan's T1.11); the manifest's `agent` line changes from `library` to `subject` then |

Minimal procedure: write or edit a `*.matter.omui.ts`; `omui-gen snapshot` it; `prepare_document` and read the problems; `describe()` to check what was understood; run the scenario on a small carrier and read channels by name.
