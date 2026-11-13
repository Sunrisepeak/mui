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

Sparse execution holds with reactions as with rules (a tile sleeps when nothing in it or within reach changed; the same bits): 32 × 16 of matter at rest has 26 of its 32 tiles of 4 × 4 asleep, and the suite's eleven worlds are the reference's bits run sparsely too. `events_between(program, cells, input, output, events)` says what happened in a step as the reference does — what became another material, which channels left 0 or returned — from the two states (which law made a thing another is the record of causes', the plan's T1.11). What it costs on the CPU (`omui-bench --filter simulation/matter`, a debug build): the reference's 64 × 64 world is 2.3 ms a step on the lattice, 550 ns a cell — what the reference costs; the scalar step reads the program's words a cell at a time. 256 × 256 of the standard definition (9 channels, 19 laws) with movement, a basin and a fire is 15.7 ms a step with 19 of 64 tiles awake, 790 ns a cell run. A world pays for what is awake; all of a large one awake is the device's kernel's to run. The device's kernel runs encoding 3 bit for bit (`xengine.cellular.rhi`). Not here yet: impulses into dynamics (an explosion pushing bodies; M6.2).

Preparation owns a validated descriptor, infers halo reach and enforces resource/offset limits. `step` accepts a tile size/order; `step_tile` writes only the requested rectangle and reads its logical halo from the full immutable field. Hosts may schedule disjoint rectangles through their existing task executor. This is not streamed halo exchange, multi-cell movement arbitration or a new task pool.

**Rigid bodies of cells** (the package's feature `rigid`, module `xengine.cellular.rigid`; the plan's §6.7): a body is a grid of solid cells that [xengine.dynamics](../dynamics)' rigid family (Box2D v3) moves as one, while the lattice keeps simulating its cells. Each frame the host stamps the bodies into the field, steps the lattice, and `push` / `lift` / `terrain` / world step / `stamp`:

```cpp
auto world = dyn::World::make({.gravity = {0, 9.8f}});                  // y down, as the field's rows
auto bodies = ca::rigid::Bodies::make(program, shape, *world, {.cell = 0.1f});
auto made = bodies->carve(cells, {x, y, w, h}, air);                     // the solid cells there become bodies
// a frame
lattice.step(shape, cells, next, …); cells.swap(next);
bodies->push(cells); bodies->lift(cells); bodies->terrain(cells); world->step(); bodies->stamp(cells);
```

- **What the lattice makes of a body's cells is the body's**: they heat, get wet, burn; a cell no longer solid (burnt to ash, melted) leaves it, and the body is re-made of what is left — exactly, as rectangles of one material each — or falls apart into several bodies (four-neighbour pieces), each moving as the old one moved at its centre.
- **Matter is conserved**: what a body moves into — a liquid, a powder — changes places with the nearest gas no body holds (within `room` cells), and that gas lies under the body until it moves away. A basin with a crate of wood and one of iron keeps every cell of its water over 600 frames.
- **The field acts on bodies**: buoyancy cell by cell (a cell is under a liquid's surface when its row, walked out of the body, meets the liquid; it displaces that liquid's weight, up, at its centre — so a body rights itself), drag on the faces a liquid wets, a blast's `pressure` on its faces (`Options::blast`). Densities are the table's: wood floats across the surface, iron sinks.
- **Terrain**: the field's own solid cells are fixed collision, a body per tile of 32, rebuilt only where a tile's solid cells changed.
- **A world of matter with bodies, as one** (`MatterWorld`): the lattice, the rigid world and the coupling stepped in their order (`step`), its cells written from outside (`place`, `set` — their tiles wake), bodies carved; `save` / `restore` / `branch` the whole of it — the field, its activity, the coupling's state and Box2D's snapshot — so a branch runs on as the original does, frame by frame (world.branch). `WorldValue` is the same world as a value — a copy is a branch — so `mlibs.simulation`'s timeline keeps it and a game's state holds it (xengine.runtime's `world.seek` and `world.branch` then come with the game). Sparse: the tiles a body was in and is in (grown by `room`) wake each frame, and the world is the same bits as with every tile run.
- Deterministic: the mapping of cells through poses is + − × and floor without contraction; two runs are the same field and bodies bit for bit (`xengine.matter.cts` coupling_test: a crate lands on stone and rests, wood floats and iron sinks with the water conserved, a plank across two pillars lit in the middle burns through and falls in two, two runs equal).

**Sparse execution** (D-167): a field pays for what is happening in it. Give `step` a byte a tile that you keep between the steps of one field, and a tile at rest is not run:

```cpp
const ca::Shape grid = ca::tiles(shape, {32, 32});
std::vector<std::uint8_t> activity(grid.width * grid.height, 0);          // every tile awake
auto stepped = program->step(shape, before, after, {.tick = tick, .activity = activity, .alternate = true});
// stepped->asleep tiles were not run; stepped->cells is what was
program->wake(shape, {32, 32}, {x, y, 1, 1}, activity);                    // a cell written from outside
```

A tile sleeps when nothing in it changed and nothing within the program's reach of it did (`Program::reach()`: the halo, and a cell where cells move); what changes wakes every tile that reads it, for the next step. Where cells move, rest is more than a step without change — the cut shifts and a spread is a coin's choice — so a tile sleeps only after it was seen under all four cuts with no block in it that *could* move, whether the coin moved it or not. That makes the rule exact: the output is the same bits as with every tile run (the tests step both side by side, on rules, a reach beyond the next tile, wrapped edges and cells that move). With `alternate` (two buffers taking turns, as a host swaps them) a tile asleep is not even copied: its cells are already in the output. On a 1024 × 1024 field with a fire's front crossing 32 of 1024 tiles a step is 1.9 ms where every tile run is 67 ms, and a field wholly at rest is 0.011 ms (a debug build, this machine; `omui-bench --filter simulation/cellular-1024`). `Statistics::cells` and `matched` count what was run. The bytes are the caller's state: keep them with the cells (a checkpoint, a branch), and zero them for a field begun anew or another program, tile size or edge. `step_tile` is not sparse (a host that schedules tiles itself decides what to run); the device's kernel runs every tile, which is the same bits.

**In pieces on the host's threads**: give a step `.parallel = &each`, where `each` is a `cellular::ForEach` — the host's parallel-for (omui's `Tasks::parallel_for`, a game runtime's pool). The kernel starts no thread; the tiles, the exchange's rows of blocks and the recount run as pieces, and the bits, the activity and the statistics are those of one after another whatever runs them (the tests run pieces in order, backwards and on four threads). Hand pieces out one at a time: what is awake clusters. A 1024 × 1024 world of the standard matter — a lake at rest, sand pouring, wood alight, lava by water, a tenth awake — is 14.7 ms a step on one thread in release, 7 ms p95 on omui's pool in a debug build (`omui-bench --filter simulation/matter-1024`; budget 16 ms, one 60 Hz frame).

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
`ExternalTexture` is next). What a cell's colour is, this kernel says as it
says what a step is — a palette's first swatch (`pixel_of`), or a shade
(`Shade`, `shade_of`, D-168): a row's base colour, its grain, tints along
channels' ranges, and the light of those that emit, in whole numbers
([CONTRACT.md](CONTRACT.md)) — so a presenter on the CPU and a painter on a
device have one rule to keep. What matter looks like is a look
([xengine.matter](../matter)'s appearance) lowered to a shade by the
presenter's feature `matter`.

## Agent surface

A field is a `Subject` of the agent kernel ([mlibs.agent](../../../../base/agent)), module `xengine.cellular.agent` (the package's feature `agent`: `xengine.cellular = { path = …, features = ["agent"] }`); every operation is decided there first (openas). Its operations are a catalog, [field.ops.omui.ts](field.ops.omui.ts) (the table `src/agent/catalog.cppm` and the listings `src/agent/field.ops.json`, `.md` are `omui-gen ops`'s); how to use them, in order, is [skill.omui.md](skill.omui.md).

| | |
|---|---|
| Subject and addresses | `field`, interface `xengine.cellular.field@0.1`. A cell is `[x, y]` from the top left, a region `[x, y, width, height]`, a channel its index; a frame is one thing done to the field — a step of the program, or a value placed |
| State | The cells (channel-major `uint32`), the steps run, which tiles sleep and the prepared program, on `mlibs.simulation`'s timeline, copied whole; the hash covers the shape, the steps and every cell |
| Observation | `field.observe`: each channel of a region as numbers — least, most, sum, cells not zero — never the cells dumped; `field.cells`: one channel of a small region, row by row (at most 4096 cells a request) |
| Control | `field.place`: one channel set to a value over a region; a frame of its own, recorded like a step, so it is taken back by seeking. An owner's `Field::edit` writes several channels in one frame, set, raised or lowered |
| Time | `field.step`, `field.seek` back within the frames kept (what is run from there is a new branch); the same requests, the same bits. A step runs the tiles awake (sparse execution, above; `Options::tile`), by the field's own count of steps as the tick; every answer that says where the field is (`step`, `place`, `seek`) says `tiles` and how many are `asleep` — all of them: nothing is happening and nothing can |
| Preview | `field.branch`: the field some steps ahead, a value placed first when one is given — observed as `field.observe` does, the field itself untouched (a copy is run) |
| Budgets and tiers | `field.observe` within a `budget` of bytes (channels left out from the last, `more` says how many; a budget below the least answer is refused with that size); `Options`: `most_steps` and `most_cells` a request, `keep` frames of history |
| Causes | `field.explain`: a cell's values, the edits on record that cover it — what was written, at which frame, by whom (the actor the session knows, never what a request claims), the latest first — and what the program changed of it in the last step, from what to what. Edits are kept beside the history (the last 256) and dropped with the branch a seek leaves. Whoever watches (`Field::watch`; several may: a `Witness` each) is told every frame at its commit (a step with its cells of a step ago, an edit with its actor, a seek): where a record of causes is made — a field of matter's is `xengine.cellular.matter.agent`'s — and nothing is recorded in a step's path |
| Not here | **Source** (which rule wrote a cell) for plain rules: `capabilities` says so, and the contract suite reports it unsupported — a field of matter says which law through its matter subject. **History** as commits and branches is the kernel's (`mlibs.agent:history`), where a design has one: a field of matter in [the material lab](../matter/lab) is played from sources that are committed, branched and merged |

```sh
mcpp test -p xengine.cellular.present     # agent_test: the agent contract's suite on a field of falling sand, then regions, seeks and branches
```

The subject's tests are [present](present)'s, which turns the feature on: a feature is tested where it is on.

### A field of matter: the `matter` subject

Where the cells are matter, a second subject stands beside `field` in the same session — module `xengine.cellular.matter.agent`, the package's feature `matter-agent` (it implies `agent` and `matter`) — and speaks of the field in matter's own words (D-169): materials by name, channels in their units, laws by name. Its catalog is [matter.ops.omui.ts](matter.ops.omui.ts) (`src/matter/agent/catalog.cppm`, `matter.ops.json`, `.md` generated); the procedure is the second part of [skill.omui.md](skill.omui.md).

```cpp
auto world = cellular::matter_agent::World::make(*matter::standard(), {64, 48}, "air");   // a field and its matter subject
mlibs::agent::SubjectSession session({world->field.get(), world->matter.get()});
world->matter->place(*program.material("wood"), {10, 40, 12, 1}, game);                   // the same edits from code, on record under the host's name
```

| | |
|---|---|
| Subject and addresses | `matter`, interface `xengine.matter.lattice@0.1`. A cell `[x, y]`, a region `[x, y, width, height]`; a material, a channel and a law by name; a law's place in the definition as a JSON pointer (`/laws/8`); amounts in the channel's unit (kelvins, not the stored integer) |
| State | The field's — it has none of its own but the record of causes; its hash is the field's |
| Observation | `matter.describe`: materials, channels with units, laws; one material's properties or one law as written. `matter.observe`: a region's cells by material, each channel's least, most and mean in its unit, cells in each named state. `matter.at`: one cell, and its properties where its state changes them (wet wood: `ignition` written 570, now 970) |
| Control | `matter.place` (a region of a material, every channel as a new instance has) and `matter.apply` (a channel `set`, `add`, `take`): each one frame of the field, taken back by `field.seek`. From code: `Matter::place`, `Matter::apply` |
| Time | The field's: `field.step`, `field.seek`. `capabilities` says the matter subject has none, and the contract suite reports those two cases unsupported for it — one clock, said once |
| Causes | The record, made at each frame's commit by the field's `Witness`: after a step, every cell is compared with the cell it was a step ago — where it was then, by the kernel's `Program::origins`, since a cell may change and change places in one step — and each whose material or named state changed is judged again by the matter kernel's `Program::why` on the step's input (the cell and what is beside it as a carrier of their own; the bath or the edge as the field has it), and what happened is kept: *became* (the law of the conversion), a named state *begun* or *ended* (the law whose flag it is; for an end, the comparison that stopped holding), with up to 8 values the law read and up to 4 neighbours' parts in it, by what the law's comparisons wanted: who **gave** (the greatest flows toward the cell of a channel the law needed more of — heat to ignite — and the parts of the near sums it needed), who **took** (the greatest flows away, where less made it so: what lava gave its heat to as it set; what cooled a fire below its ignition point), and what was **beside** it where a near sum fell short (the water over a fire that has no air) — each with what it was and which named states it was in. Edits are on record with their actor. The last 16384 are kept (`Options::records_kept`); a seek drops what came after |
| Why, and why not | `matter.explain {cell}`: what is on record of the cell, the latest first, each neighbour with a part (`gave`, `took`, `beside`) followed to its own latest record (`because`), `depth` deep (2; 0 … 6), and the edit that set what the law read (`applied`, with `by`). With `law`: that law judged for the cell as it is now — `holds`, or the comparison that `fails`, and every value read |
| What happened | `matter.events`: the record as a stream, oldest first — `since` a frame, of a `name` (`ignite`, `go_out`, `melt`, `boil`, `freeze`, `condense`, `burn_out`, `placed`, `applied`), in a `region`; within a `budget` the latest are kept and `more` counts the rest |
| Preview | `matter.branch`: a copy some steps ahead, a material placed or a quantity applied first — a region's materials now and then, what would happen counted by name, and the first six events in full. The same judgement a commit makes, on a state that is not the field's |
| Budgets | `budget` in bytes on `observe` (channels left out from the last, then materials from the fewest), `explain` (fewer records) and `events` (the latest); below the least answer a request is refused with that size. `Options::most_steps` a branch |
| Not here | A record older than the last kept; a cause further than a cell's neighbours in one step (it is followed giver by giver instead). Other carriers of matter (bodies, particles) get their subjects where they hold their state; what is shared is the kernel's `why` |

The chain as an agent reads it (the test's scene: a strip of wood on stone, its left end set to 700 K by the session's actor, 120 steps; shortened):

```json
{"op":"matter.explain","cell":[4,3],"depth":3}
{"cell":[4,3],"frame":123,"now":{"material":"wood","temperature":874.023,"burned":5,"burning":1},
 "records":[
  {"frame":119,"step":116,"cell":[4,3],"what":"ignite","channel":"burning",
   "law":{"name":"burns","at":"/laws/8","when":"temperature >= ignition && burned < fuel && near.oxygen > 0"},
   "read":{"temperature":570.754,"ignition":570,"burned":0,"fuel":400,"near.oxygen":1},
   "gave":[
    {"cell":[3,3],"what":"temperature","amount":8.62891,"was":"wood","states":["burning"],
     "because":{"frame":74,"step":71,"cell":[3,3],"what":"ignite", …,
      "gave":[{"cell":[2,3],"what":"temperature","amount":8.5625,"was":"wood","states":["burning"],
        "because":{"frame":4,"step":1,"cell":[2,3],"what":"ignite", …, "read":{"temperature":700, …},
         "applied":{"frame":3,"what":"applied","channel":"temperature","set":700,"region":[2,3,1,1],
                    "by":{"kind":"agent","name":"the one who lit it"}}}}]}},
    {"cell":[4,2],"what":"near.oxygen","amount":1,"was":"air"}]},
  {"frame":2,"what":"placed","material":"wood","region":[2,3,8,1],"by":{"kind":"agent","name":"the one who lit it"}}]}
```

What holds the record to the kernel ([matter's CTS](../matter/cts), `tests/agent_test.cpp`): a cell's slice steps to the field's own bits — 564 480 values over 160 steps of a busy scene, closed and open to a bath; and what is on record of a step is exactly what the reference step says happened in it (`xengine.matter`'s `step` with its events, on the whole grid) — with cells at rest and with cells moving. A branch's events are the events of the same frames when they are stepped.

What it costs to be watched (`simulation/matter-256-causes`, a debug build): a 256×256 field with water that does not rest and a fire along wood steps in 18.3 ms with its record against 17.8 ms without — the commit looks only at the tiles that were awake, asks the kernel where each of their cells came from, and calls `why` only where something happened. A field no one watches costs what it did.

And the emergence set (the plan's §9.3) asked why, each phenomenon through the session an agent has: a fire that went out under water reads `near.oxygen` 0 with water `beside` it, placed by the actor who poured it; lava that set had its heat `took` by the water; ice melted by what burning wood `gave`; water frozen by a cold an actor `applied`; oil caught from oil alight, and the water under it boiled by its heat; wood at the far end of an iron bar lit by what the iron `gave`; a trail of gunpowder, grain from grain; and alcohol — two rows of properties, no law of its own — boiled by the law water boils by.

```sh
mcpp test -p xengine.matter.cts agent_test   # the agent contract's suite on a field of matter; the chain of causes, why not, events, branches; the record against the kernel
```
