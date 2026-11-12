# xengine.cellular

A prepared integer rule library for dense 2D fields. Channels may represent material, phase, age, flags or other application-defined state; the kernel has no built-in fire, pet, UI or game rules. It depends only on shared trace/log foundations and the standard library. The clock/history is also optional.

```cpp
import xengine.cellular;
namespace ca = xengine::cellular;
const std::array rules{
    ca::Rule{{ca::Condition::neighbors({0, {1, 1}}, ca::cardinal, {1, 4})},
             {{0, ca::WriteKind::set, 1}}}
};
auto program = ca::Program::prepare(1, rules);
if (!program) { /* report code, rule and item */ return; }
auto result = program->step({width, height}, before, after);
if (result) before.swap(after); // host publishes one complete step
```

Fields are contiguous channel-major `uint32_t` values: `[channel][y][x]`. All predicates and writes read the same immutable input. Rules are ordered; the first match wins. Conditions are ANDed; neighborhood counts use explicit offsets. Writes set, saturating-add or saturating-subtract; untouched channels copy through. Empty writes form a barrier. These are discrete, exact integer transitions, not a continuous physical/chemical solver.

**Aggregates and linear writes** (encoding 2; the first of the staged program's stages, D-167). A condition may also ask for a neighbourhood's *sum*, *least* or *greatest* value of a channel (`Condition::sum`, `minimum`, `maximum`), not only how many of its values are in a range; and a write's amount may be a linear expression of the cell and its neighbourhood — `value` plus terms, each a source (the cell's own value, or a count, sum, least or greatest over a neighbourhood) times a factor, shifted right, added or taken:

```cpp
// Heat spreads: next = (4 × self + the four neighbours) / 8.
const std::vector<ca::Rule> diffuse{{{}, {{0, ca::WriteKind::set, 0, {ca::Term::self(0, 4, 3), ca::Term::sum(0, ca::cardinal, 1, 3)}}}}};
```

Every step of it is 32-bit unsigned and saturating — a product stops at the largest number before its shift, sums stop there, a difference stops at 0 — so it is the same bits wherever it runs, and a device's shader needs no wider integer. A program that uses none of this keeps encoding 1, word for word what it was, which is what the device's kernel runs today; one that does is encoding 2 and runs on this CPU reference (the device's kernel refuses it as unsupported until it has the same stages). 
**Transfers** (encoding 2; the stage after the rules). A quantity that flows between a cell and each of its neighbours, from the one that has more to the one that has less — heat, moisture, charge:

```cpp
// An eighth of each difference a step, to each of four neighbours; a cell conducts as channel 1 says (0: not at all).
const std::array<ca::Transfer, 1> heat{{{.channel = 0, .offsets = {ca::cardinal.begin(), ca::cardinal.end()}, .multiply = 1, .shift = 5, .by = 1, .by_limit = 4}}};
auto program = ca::Program::prepare(2, ca::Stages{.rules = rules, .transfers = heat});
```

Both ends of a pair compute the flow from the same two values, so what leaves one cell is what arrives at the other; and preparation refuses a transfer whose neighbourhood could together take more than a difference (`by_limit × multiply × neighbours` above `2^shift`), so nothing saturates. What the field holds of the quantity is then what it held, exactly, step after step — at a wrapped edge it goes round, at a clamped one nothing crosses, at a constant one the outside is a bath at that value. Rules and transfers read the same input; where a rule writes the channel in the same step its write comes first and the flows are added to it. Flows are whole numbers: a pair whose difference is too small to move anything stays as it is.

**An exchange** (encoding 2; the stage after the transfers): cells that move — sand that falls and piles, a liquid that finds its level, a lighter liquid lying on a heavier, a gas going up. The kernel knows two channels, how heavy a cell is and how it may move (`falls`, `slides`, `spreads`); sand, water, oil and smoke are an application's values of them:

```cpp
// channel 0 the weight, 1 the mobility; every channel goes with its cell
const std::array<ca::Exchange, 1> moving{{{.weight = 0, .mobility = 1}}};
auto program = ca::Program::prepare(channels, ca::Stages{.rules = rules, .transfers = heat, .exchanges = moving});
auto stepped = program->step(shape, before, after, {.tick = tick});   // the tick says where the blocks are cut
```

The field is cut into blocks of 2 × 2, and the cut shifts each tick through its four positions. Within a block: a cell that falls sinks below a lighter one that falls; a cell that slides, resting, goes diagonally down past a lighter neighbour; a cell that spreads changes places with a lighter one beside it, or not, with equal chance — a number of (seed, tick, block, row) alone, `mlibs.math`'s `mix32`, so the same field at the same tick moves the same way, here and in a shader. A cell moves once a tick. Each cell is in one block, so no two cells contend for a place, blocks may be done in any order or at once, and what moves is kept by construction: whole cells change places, nothing is made or lost. Moves are decided from the step's input, as the rules and transfers are, and carry what those made of each cell. A program that moves cells steps the whole field (`step_tile` refuses it). A cell moves at most one place a tick and waits for the cut to come round: speed is ticks a frame, an application's choice.

```
 a column of six grains, at rest after 19 ticks        water, oil and smoke after 1500
 ...........                                           .....^^^....
 ....ss.....                                           .ooooo..oo.o
 ...ssss....                                           ~~~~~~~~~~~~
                                                       ~~~~~~~~~~~~
```

**A table and reactions** (encoding 3; D-167): what a cell *is*, looked up. One channel is a key into a table of rows × columns, and a value may be a column of the cell's row — or of its neighbours' rows, summed. Where the rules are tried in order and the first that matches writes, every reaction that holds acts: what they add and take accumulates, the first to set a channel gives its base, a flag's channel is 1 while one that flags it holds, and a cell becomes the row its own column names. So a kind of thing is a row of numbers and adding one adds no rule:

```cpp
ca::Reactions staged;
staged.table = {.key = 0, .columns = 3, .values = {/* row 0 */ 5, 20, 1,  /* row 1 */ 0, 0, 1}};   // gain, limit, what it becomes
staged.near = {ca::cardinal.begin(), ca::cardinal.end()};
staged.reactions = {
    {.when = {{{{ca::Read::channel, 1}}, {{ca::Read::lookup, 1}}, ca::Compare::less}},          // level < its row's limit
     .changes = {{1, ca::ChangeKind::add, {{ca::Read::lookup, 0}}}}, .flag = 2},                  // level += its row's gain; flagged
    {.when = {{{{ca::Read::channel, 1}}, {{ca::Read::lookup, 1}}, ca::Compare::greater_equal}}, .become = 2},
};
const std::array<ca::Transfer, 1> heat{{{.channel = 3, .offsets = beside, .shift = 4, .by_limit = 4, .by_column = 0}}};   // conducted by a column
auto program = ca::Program::prepare(channels, ca::Stages{.transfers = heat, .reactions = &staged});
```

An amount is a sum of terms — a number, a channel of the cell, a column of its row as written (`lookup`) or as the adjustments left it (`derived`: a column raised or lowered by the cell's state before the reactions read it), the neighbourhood's sum of a channel or of a column — each × a number, >> a shift, added or taken; a condition compares two amounts. A transfer may be conducted by a column (`by_column`), an exchange may take how heavy a cell is and how it may move from two columns (`by_table`), and a channel may start again when a cell becomes another row (`Reset`: at the new row's own value where it has one). A cell whose key is no row takes no part: no reaction acts on it, nothing flows to or from it, it does not move, and it is nothing to its neighbours — the outside of a world is `Edge::constant` with a value that is no row. One step is then: the rules, the reactions of the same input onto what the rules made, the flows, the resets, and the moves. The kernel knows a key and numbers; that a row is a material is whoever wrote the table's.

**As a carrier of matter** (the package's feature `matter`, module `xengine.cellular.matter`; D-165): `lower(program)` makes of an [xengine.matter](../matter) program a lattice program — its materials' properties are the table (and one column more: how each phase may move), its transfer laws transfers by a column, its adjustments, reactions and conversions the reactions stage, its channels the lattice's in the same order. Without movement a step is the reference step's bits: [xengine.matter.cts](../matter/cts) holds the lattice to it, every tile run and sparsely, in eleven worlds (heat, fire, wet and dry, the changes of phase, charge, soups of every material with and without wrapped edges, what is no material, values at the ends of 32 bits, a material added as rows, a definition using every kind of read and write). With it, cells change places by `density` and `phase` — a powder falls and slides, a liquid also levels, a gas changes places up and sideways, a solid stays (it is dynamics' to move):

```cpp
import xengine.cellular.matter;
auto lattice = ca::lower(*matter::standard());                                  // channel c of a cell is matter's channel c
auto stepped = lattice->step(shape, cells, next, {.boundary = ca::nothing_beyond, .tick = tick, .activity = activity});
```

**The emergence set** ([matter/cts/tests/emergence_test.cpp](../matter/cts/tests/emergence_test.cpp); the plan's §9.3, its 2D part): nine phenomena as scenes with no interface, all of the one standard definition — 17 materials, 19 laws, none naming a material — and no scene adding a law. The worlds are open to air at 293 K beyond their edges unless a scene closes one.

| | What happens | By |
|---|---|---|
| 1 | sand sinks through water and piles, oil comes up and lies on it, smoke goes to the top, stone and wood stay | density, phase |
| 2 | a strip of wood lit at one end burns piece by piece (caught at ticks 1, 80, 132, 183, 233, 283, 332, 382); wood of wetness 10 does not light at 600 K where dry wood does | conduction, an ignition point, wetness raising it |
| 3 | 16 of water let go over 8 of burning wood: out after 16 ticks with every piece left (without the water all are ash); 13 of the water become steam, rise to the top, and all 16 are water again | no air beside what is covered, wetness, a boiling point, latent heat, a gas rising |
| 4 | water over lava boils (14 of steam at once), the steam condenses where the air is cool, the lava sets as stone | conduction, a boiling and a melting point |
| 5 | in a room at 250 K a row of ice beside a fire melts nearest first (ticks 75, 104, 160, 252, 348, 419; the two farthest are ice still at 500); water let into that room is ice after 15 ticks | conduction, a melting point |
| 6 | 28 of oil let go under water come up, lie on it, and lit at one end burn across — 17 alight at once, all smoke after 643 ticks, 86 % of the burning with water under it | 1 and 2 |
| 7 | in a closed room, wood at the far end of an iron bar of six from a 1250 K source catches at tick 430, of a stone bar at 1046; charge goes along iron into water and not into wood | how well a thing conducts heat, and charge |
| 8 | a trail of 10 of gunpowder is gone 31 ticks after its end is lit (wood: still burning at 400); its blast is pressure, which pushes a body of rubber standing two cells off 34 cells away, where beside burning wood it stands | a burn rate, a flame temperature, its own oxygen, pressure as a quantity; `push_on` to the body |
| 12 | alcohol — two rows added, no law — comes up through water and lies on it, boils over a hot stone, its vapour rises | every law, by its properties |

And one that no one wrote: nine grains of sand dropped into a basin full to the brim push water over its wall, the water runs along the floor to a strip of wood, and that wood will not light at 700 K where the same wood in the same world without the sand burns to ash.

```
 after 300 ticks: the sand is in, the water is over the wall      the flame at 700 K: with the sand, 8 of wood left;
 ~#~~~s~~~~#.....................                                  without it, 8 of ash
 ~#~~sss~~~#.....................
 ~#~sssss~~#.~.~~~~~.wwwwwwww....
 ################################
```

What the scenes showed of the model, and what was done: a fire heated without limit and melted the stone under it — what burns now heats to its `flame` temperature and no further; a closed world keeps all its heat (steam never condensed, a bar's far end caught because the whole room was hot) — a world's edge may be a bath (`open_to`: air at its temperature); water boiled away under a slick in a tenth of the burn — boiling takes its latent heat, at the boiling point, and condensing gives it back. Still so: `thermal` runs from 1 (air) to 8 (iron) where iron is thousands of times air, so 7 needs its closed room — in the open a bare bar gives its heat to the air first; pressure spreads as heat does, not as a wave, and pushes bodies, not loose cells; every cell holds the same heat a kelvin.

**An edge that is a bath, and a push**: `Boundary::values` (one a channel) makes what is beyond a constant edge a cell of its own — flows reach it and conditions see it; `cellular::open_to(program.fresh(1, air))` is a world of matter under an open sky. `push_on(shape, channel's cells, rectangle)` is what a quantity does to a rectangle from outside — its left side's sum less its right's, above less below: the field's pressure on what stands in it, for whoever moves that (a body, which the kernel does not know).

Sparse execution holds with reactions as with rules (a tile sleeps when nothing in it or within reach changed; the same bits): 32 × 16 of matter at rest has 26 of its 32 tiles of 4 × 4 asleep, and the suite's eleven worlds are the reference's bits run sparsely too. `events_between(program, cells, input, output, events)` says what happened in a step as the reference does — what became another material, which channels left 0 or returned — from the two states (which law made a thing another is the record of causes', the plan's T1.11). What it costs on the CPU (`omui-bench --filter simulation/matter`, a debug build): the reference's 64 × 64 world is 2.3 ms a step on the lattice, 550 ns a cell — what the reference costs; the scalar step reads the program's words a cell at a time. 256 × 256 of the standard definition (9 channels, 19 laws) with movement, a basin and a fire is 15.7 ms a step with 19 of 64 tiles awake, 790 ns a cell run. A world pays for what is awake; all of a large one awake is the device's kernel's to run. Not here yet: the device's kernel for encoding 3 (T1.8), and impulses into dynamics (an explosion pushing bodies).

Preparation owns a validated descriptor, infers halo reach and enforces resource/offset limits. `step` accepts a tile size/order; `step_tile` writes only the requested rectangle and reads its logical halo from the full immutable field. Hosts may schedule disjoint rectangles through their existing task executor. This is not streamed halo exchange, multi-cell movement arbitration or a new task pool.

**Sparse execution** (D-167): a field pays for what is happening in it. Give `step` a byte a tile that you keep between the steps of one field, and a tile at rest is not run:

```cpp
const ca::Shape grid = ca::tiles(shape, {32, 32});
std::vector<std::uint8_t> activity(grid.width * grid.height, 0);          // every tile awake
auto stepped = program->step(shape, before, after, {.tick = tick, .activity = activity, .alternate = true});
// stepped->asleep tiles were not run; stepped->cells is what was
program->wake(shape, {32, 32}, {x, y, 1, 1}, activity);                    // a cell written from outside
```

A tile sleeps when nothing in it changed and nothing within the program's reach of it did (`Program::reach()`: the halo, and a cell where cells move); what changes wakes every tile that reads it, for the next step. Where cells move, rest is more than a step without change — the cut shifts and a spread is a coin's choice — so a tile sleeps only after it was seen under all four cuts with no block in it that *could* move, whether the coin moved it or not. That makes the rule exact: the output is the same bits as with every tile run (the tests step both side by side, on rules, a reach beyond the next tile, wrapped edges and cells that move). With `alternate` (two buffers taking turns, as a host swaps them) a tile asleep is not even copied: its cells are already in the output. On a 1024 × 1024 field with a fire's front crossing 32 of 1024 tiles a step is 1.9 ms where every tile run is 67 ms, and a field wholly at rest is 0.011 ms (a debug build, this machine; `omui-bench --filter simulation/cellular-1024`). `Statistics::cells` and `matched` count what was run. The bytes are the caller's state: keep them with the cells (a checkpoint, a branch), and zero them for a field begun anew or another program, tile size or edge. `step_tile` is not sparse (a host that schedules tiles itself decides what to run), and neither is the device's kernel yet (the plan's T1.8).

Use [the optional RHI kernel](rhi) for GPU execution (its suite and the field's device picture run on a native device in [sdl](sdl)). It consumes the same prepared word descriptor; choosing this CPU package does not import RHI, shaders, renderer, UI or games. [CONTRACT.md](CONTRACT.md) records the encoding, boundaries and error semantics.

Minimal SOP: define typed channels/rules and bounded shape; prepare and inspect `channels/halo/limits/words`; allocate two fields; step and inspect `Statistics` plus complete output; publish/swap; record inputs/configuration/tick in a host-owned history when replay is needed. No GUI is required. Scripts/Agent requests configure batches, not per-cell callbacks. Restore through `xengine.simulation::Timeline` only with deeply owned state; visual review separately uses the shared renderer and whole-process samples. An openas provider and graphical rule editor remain subsequent consumers.

`mcpp test --locked -p xengine.cellular` checks an independent scalar oracle across tile orders/sizes, boundaries, saturation, failed writes and history reconstruction. Shared trace category: `simulation`, spans `simulation.cellular.prepare/step/tile`, changed-cell counter; cold preparation uses the same log category. `simulation/cellular-256` and `simulation/checkpoint-64` in the common benchmark suite check bounded CPU workloads and zero allocations, not Noita-scale performance.

## Independent boundary and compatibility

Inspector and the GPU adapter consume this rule model independently of the
clock/history library; caller-owned ticks and buffers are sufficient. Its
separate manifest makes that omission real. Individual rules, conditions,
channel operators and tile helpers stay inside this package.

The kernel's module is `xengine.cellular` (no forwarding names): it imports
no clock, no RHI and no renderer, and taken alone the package depends on the
small tools only. The lattice as a carrier of matter is a third module,
`xengine.cellular.matter`, behind the feature `matter`: it takes the matter
kernel (no JSON) and nothing else. The field as an agent's subject is a second module,
`xengine.cellular.agent` (below), behind the package's feature `agent`: it
takes the clock and history library and the agent kernel — both mlibs,
neither a renderer nor an interface — and only for a consumer that asks.

Ownership around it (convergence plan §6.10, D-132; D-165): materials and
what they do are [xengine.matter](../matter)'s definitions — data — lowered to
this kernel's table and reactions, not rules inside it;
putting a field on screen is a small adapter between this package and
xrender's resources, [present](present): one `ImageStore` image a pixel a
cell, recoloured, re-uploaded and redrawn only where `Execution::changed_tiles`
says a tile changed, drawn by one command (the GPU kernel's output as one
`ExternalTexture` is next).

## Agent surface

A field is a `Subject` of the agent kernel ([mlibs.agent](../../../../base/agent)), module `xengine.cellular.agent` (the package's feature `agent`: `xengine.cellular = { path = …, features = ["agent"] }`); every operation is decided there first (openas). Its operations are a catalog, [field.ops.omui.ts](field.ops.omui.ts) (the table `src/agent/catalog.cppm` and the listings `src/agent/field.ops.json`, `.md` are `omui-gen ops`'s); how to use them, in order, is [skill.omui.md](skill.omui.md).

| | |
|---|---|
| Subject and addresses | `field`, interface `xengine.cellular.field@0.1`. A cell is `[x, y]` from the top left, a region `[x, y, width, height]`, a channel its index; a frame is one thing done to the field — a step of the program, or a value placed |
| State | The cells (channel-major `uint32`), the steps run, which tiles sleep and the prepared program, on `mlibs.simulation`'s timeline, copied whole; the hash covers the shape, the steps and every cell |
| Observation | `field.observe`: each channel of a region as numbers — least, most, sum, cells not zero — never the cells dumped; `field.cells`: one channel of a small region, row by row (at most 4096 cells a request) |
| Control | `field.place`: one channel set to a value over a region; a frame of its own, recorded like a step, so it is taken back by seeking |
| Time | `field.step`, `field.seek` back within the frames kept (what is run from there is a new branch); the same requests, the same bits. A step runs the tiles awake (sparse execution, above; `Options::tile`), by the field's own count of steps as the tick; every answer that says where the field is (`step`, `place`, `seek`) says `tiles` and how many are `asleep` — all of them: nothing is happening and nothing can |
| Preview | `field.branch`: the field some steps ahead, a value placed first when one is given — observed as `field.observe` does, the field itself untouched (a copy is run) |
| Budgets and tiers | `field.observe` within a `budget` of bytes (channels left out from the last, `more` says how many; a budget below the least answer is refused with that size); `Options`: `most_steps` and `most_cells` a request, `keep` frames of history |
| Not here | **Causes** (why a cell holds what it holds, who placed it) and **source** (which rule wrote a cell): `capabilities` says so, and the contract suite reports them unsupported. They come with the matter kernel's record of causes (the plan's T1.11). **History** as commits and branches is the kernel's, when a design adapter needs it |

```sh
mcpp test -p xengine.cellular.present     # agent_test: the agent contract's suite on a field of falling sand, then regions, seeks and branches
```

The subject's tests are [present](present)'s, which turns the feature on: a feature is tested where it is on.
