# xengine.simulation.cellular

A prepared integer rule library for dense 2D fields. Channels may represent material, phase, age, flags or other application-defined state; the kernel has no built-in fire, pet, UI or game rules. It depends only on shared trace/log foundations and the standard library. The clock/history is also optional.

```cpp
import xengine.simulation.cellular;
namespace ca = xengine::simulation::cellular;
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

Use [the optional RHI kernel](../../../gfx/simulation/cellular/rhi) for GPU execution. It consumes the same prepared word descriptor; choosing this CPU package does not import RHI, shaders, renderer, UI or games. [CONTRACT.md](CONTRACT.md) records the encoding, boundaries and error semantics.

Minimal SOP: define typed channels/rules and bounded shape; prepare and inspect `channels/halo/limits/words`; allocate two fields; step and inspect `Statistics` plus complete output; publish/swap; record inputs/configuration/tick in a host-owned history when replay is needed. No GUI is required. Scripts/Agent requests configure batches, not per-cell callbacks. Restore through `xengine.simulation::Timeline` only with deeply owned state; visual review separately uses the shared renderer and whole-process samples. An openas provider and graphical rule editor remain subsequent consumers.

`mcpp test --locked -p xengine.simulation.cellular` checks an independent scalar oracle across tile orders/sizes, boundaries, saturation, failed writes and history reconstruction. Shared trace category: `simulation`, spans `simulation.cellular.prepare/step/tile`, changed-cell counter; cold preparation uses the same log category. `simulation/cellular-256` and `simulation/checkpoint-64` in the common benchmark suite check bounded CPU workloads and zero allocations, not Noita-scale performance.

## Independent boundary and compatibility

Inspector and the GPU adapter consume this rule model independently of the
clock/history library; caller-owned ticks and buffers are sufficient. Its
separate manifest makes that omission real. Individual rules, conditions,
channel operators and tile helpers stay inside this package.

The same package exports `openmui.simulation.cellular` and the old
`omui::simulation::cellular` alias. It does not import `xengine.simulation` to
create that compatibility namespace. CPU-only consumers therefore keep their
no-clock/no-RHI/no-renderer dependency boundary under either source spelling.
