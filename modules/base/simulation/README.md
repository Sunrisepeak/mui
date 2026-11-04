# openmui.simulation

Independent fixed-step clocks and owned checkpoint/input replay. This is the single implementation extracted from `mgame.sim`; the latter retains its C++ source-compatible aliases and plain-data game constraint. Neither package owns a renderer, device, network endpoint or application world.

```cpp
import openmui.simulation;
omui::simulation::Clock clock(120);
omui::simulation::Timeline<Model> history(initial, 3600, 60);
for (int n = clock.advance(now_seconds); n > 0; --n) history.step(input);
const auto& state = history.state();
if (history.seek(frame)) { /* observe reconstructed state; no external effects */ }
```

`Model` defines owned, copyable `State`, trivially copyable/default-constructible `Input`, and `static void step(State&, const Input&)`. State copies must not alias mutable storage. The constructor fills every snapshot slot from the initial state; fixed-shape vectors can then reuse capacity. Model operations must remain deterministic and free of live audio, network, UI or business side effects. Seek restores a snapshot and replays inputs; it is not constant-time reverse integration.

The host chooses bounded, trusted history capacities, records semantic inputs, advances its clock and publishes state. Keep the same model/configuration within a history; changing rules or topology requires an explicit rebuild/reset. Cancel an external job between complete steps; restoring a checkpoint is not undoing external actions. A failed seek leaves state unchanged. See [CONTRACT.md](CONTRACT.md) for limits and clock behavior.

| Optional package | Role |
|---|---|
| [cellular](cellular) | Prepared integer channel/neighborhood rules, immutable input and tiled output; can also be used without this clock/history |

Minimal Agent/human SOP: prepare the model and a bounded initial state; advance and inspect states without a GUI; record frame/input/configuration identity; seek and compare complete states throughout a process; publish to a separately selected renderer for visual review. The library has native C++ operations, not an already-shipped openas provider. Trace spans `simulation.step`/`simulation.seek` and log category `simulation` use the shared observability libraries.

Run `mcpp test --locked -p openmui.simulation -p mgame.sim`. `openmui.showcase.widget_breakout.sim` checks an existing real game consumer. Snapshot/step allocation budgets are in the common `openmui.bench` suite, not a second benchmark harness.
