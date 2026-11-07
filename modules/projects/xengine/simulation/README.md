# xengine.simulation

Independent fixed-step clocks and owned checkpoint/input replay. This is the single implementation shared by native applications, Inspector and games. It owns no renderer, device, network endpoint or application world.

```cpp
import xengine.simulation;
xengine::simulation::Clock clock(120);
xengine::simulation::Timeline<Model> history(initial, 3600, 60);
for (int n = clock.advance(now_seconds); n > 0; --n) history.step(input);
const auto& state = history.state();
if (history.seek(frame)) { /* observe reconstructed state; no external effects */ }
```

`Model` defines owned, copyable `State`, trivially copyable/default-constructible `Input`, and `static void step(State&, const Input&)`. State copies must not alias mutable storage. The constructor fills every snapshot slot from the initial state; fixed-shape vectors can then reuse capacity. Model operations must remain deterministic and free of live audio, network, UI or business side effects. Seek restores a snapshot and replays inputs; it is not constant-time reverse integration.

The host chooses bounded, trusted history capacities, records semantic inputs, advances its clock and publishes state. Keep the same model/configuration within a history; changing rules or topology requires an explicit rebuild/reset. Cancel an external job between complete steps; restoring a checkpoint is not undoing external actions. A failed seek leaves state unchanged. See [CONTRACT.md](CONTRACT.md) for limits and clock behavior.

## Soft bodies

`xengine.simulation.bodies` is the particle and constraint solver animation and physics share (convergence plan §6.9, D-131): a `SoftBody` is a ring of particles held by its edges' lengths, its enclosed area and the shape it was made in (shape matching), stepped by small-step XPBD under gravity against half-plane colliders with restitution and friction; a `Pin` is a dragging hand. `SoftBodyModel` is a `Model`, so a body runs in a `Timeline` with checkpoints and exact replay; stepping allocates nothing. hanim's `Engine::simulate` makes any part of any object such a body and carries its geometry on a cage over the ring. `tests/bodies_test.cpp` drops a ring on the ground (it flattens, bounces, holds its area within 5 %, rests on top), drags one, and replays it bit for bit.

| Optional package | Role |
|---|---|
| [cellular](cellular) | Prepared integer channel/neighborhood rules, immutable input and tiled output; can also be used without this clock/history |

Minimal Agent/human SOP: prepare the model and a bounded initial state; advance and inspect states without a GUI; record frame/input/configuration identity; seek and compare complete states throughout a process; publish to a separately selected renderer for visual review. The library has native C++ operations, not an already-shipped openas provider. Trace spans `simulation.step`/`simulation.seek` and log category `simulation` use the shared observability libraries.

Run `mcpp test --locked -p xengine.simulation`. `omui.showcase.widget_breakout.sim` checks an existing real game consumer. Snapshot/step allocation budgets are in the common `omui.bench` suite, not a second benchmark harness.

## Independent boundary and compatibility

The clock and owned history are used by Inspector and game consumers, and can
run any valid model without cellular rules, graphics or UI. Their independent
manifest keeps those domains optional. They share log/trace foundations rather
than maintain another observer system. Internal clock/history helpers stay in
this package.

`openmui.simulation` remains an in-package forwarding module. The old
`omui::simulation::Clock`, `Model` and `Timeline` use declarations of the exact
canonical entities. This real compatibility namespace allows the independent
cellular package to expose its own alias without importing the clock package.