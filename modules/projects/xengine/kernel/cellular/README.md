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

Preparation owns a validated descriptor, infers halo reach and enforces resource/offset limits. `step` accepts a tile size/order; `step_tile` writes only the requested rectangle and reads its logical halo from the full immutable field. Hosts may schedule disjoint rectangles through their existing task executor. This is not streamed/sparse halo exchange, multi-cell movement arbitration or a new task pool.

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
small tools only. The field as an agent's subject is a second module,
`xengine.cellular.agent` (below), behind the package's feature `agent`: it
takes the clock and history library and the agent kernel — both mlibs,
neither a renderer nor an interface — and only for a consumer that asks.

Ownership around it (convergence plan §6.10, D-132): material and reaction
presets are an optional data package beside this kernel, not rules inside it;
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
| State | The cells (channel-major `uint32`), the steps run and the prepared program, on `mlibs.simulation`'s timeline, copied whole; the hash covers the shape, the steps and every cell |
| Observation | `field.observe`: each channel of a region as numbers — least, most, sum, cells not zero — never the cells dumped; `field.cells`: one channel of a small region, row by row (at most 4096 cells a request) |
| Control | `field.place`: one channel set to a value over a region; a frame of its own, recorded like a step, so it is taken back by seeking |
| Time | `field.step`, `field.seek` back within the frames kept (what is run from there is a new branch); the same requests, the same bits |
| Preview | `field.branch`: the field some steps ahead, a value placed first when one is given — observed as `field.observe` does, the field itself untouched (a copy is run) |
| Budgets and tiers | `field.observe` within a `budget` of bytes (channels left out from the last, `more` says how many; a budget below the least answer is refused with that size); `Options`: `most_steps` and `most_cells` a request, `keep` frames of history |
| Not here | **Causes** (why a cell holds what it holds, who placed it) and **source** (which rule wrote a cell): `capabilities` says so, and the contract suite reports them unsupported. They come with the matter kernel's record of causes (the plan's T1.11). **History** as commits and branches is the kernel's, when a design adapter needs it |

```sh
mcpp test -p xengine.cellular.present     # agent_test: the agent contract's suite on a field of falling sand, then regions, seeks and branches
```

The subject's tests are [present](present)'s, which turns the feature on: a feature is tested where it is on.
