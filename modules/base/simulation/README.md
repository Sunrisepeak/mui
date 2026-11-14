# mlibs.simulation

The stepping substrate every simulation runs on (D-133): fixed-step clocks, owned checkpoint/input timelines (seek, replay, rollback, branches) and the tiers a simulation declares. **It holds no solver**: physics is `xengine.dynamics` (soft bodies today in `xengine.simulation`, until D-133 §7.1 S2), cellular rules `xengine.cellular`, a game its own model. Domain-agnostic, so in mlibs; it owns no renderer, device, network endpoint or application world.

```cpp
import mlibs.simulation;
mlibs::simulation::Clock clock(120);
mlibs::simulation::Timeline<Model> history(initial, 3600, 60);
for (int n = clock.advance(now_seconds); n > 0; --n) history.step(input);
const auto& state = history.state();
if (history.seek(frame)) { /* observe reconstructed state; no external effects */ }
```

`Model` defines owned, copyable `State`, trivially copyable/default-constructible `Input`, and `static void step(State&, const Input&)`. State copies must not alias mutable storage. The constructor fills every snapshot slot from the initial state; fixed-shape vectors can then reuse capacity. Model operations must remain deterministic and free of live audio, network, UI or business side effects. Seek restores a snapshot and replays inputs; it is not constant-time reverse integration.

The host chooses bounded, trusted history capacities, records semantic inputs, advances its clock and publishes state. Keep the same model/configuration within a history; changing rules or topology requires an explicit rebuild/reset. Cancel an external job between complete steps; restoring a checkpoint is not undoing external actions. A failed seek leaves state unchanged. See [CONTRACT.md](CONTRACT.md) for limits and clock behavior.

**Tiers** (`Tier`: fidelity, determinism, backend, substeps): what a simulation, a scene or a solver family promises. Only a cross-platform or fixed-point deterministic CPU reference is `authoritative()` — what a rollback or an agent's replay relies on; a GPU tier is an acceleration.

Users: hanim (its soft bodies' history), the breakout (its game is a model), Inspector (the cellular workspace's clock and history), xengine.ui (the game panel's clock), benchmarks. Trace spans `simulation.step`/`simulation.seek` and log category `simulation` use the shared observability libraries.

Minimal Agent/human SOP: prepare the model and a bounded initial state; advance and inspect states without a GUI; record frame/input/configuration identity; seek and compare complete states throughout a process; publish to a separately selected renderer for visual review.

Run `mcpp test -p mlibs.simulation`. Snapshot/step allocation budgets are in the common `omui.bench` suite.
