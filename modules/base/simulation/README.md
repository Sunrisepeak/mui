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

| Optional package | Role |
|---|---|
| [cellular](cellular) | Prepared integer channel/neighborhood rules, immutable input and tiled output; can also be used without this clock/history |

Minimal Agent/human SOP: prepare the model and a bounded initial state; advance and inspect states without a GUI; record frame/input/configuration identity; seek and compare complete states throughout a process; publish to a separately selected renderer for visual review. The library has native C++ operations, not an already-shipped openas provider. Trace spans `simulation.step`/`simulation.seek` and log category `simulation` use the shared observability libraries.

Run `mcpp test --locked -p xengine.simulation`. `openmui.showcase.widget_breakout.sim` checks an existing real game consumer. Snapshot/step allocation budgets are in the common `openmui.bench` suite, not a second benchmark harness.

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

`mgame.sim` now also lives in this package (`src/legacy_mgame.cppm`), retaining
`mgame::Clock`, the trivial-state `mgame::Game` constraint and
`mgame::Timeline`. Its three original tests moved to
`tests/mgame_compatibility_test.cpp`. There is no `mgame/sim/mcpp.toml`, game
runtime dependency or second implementation. Update manifests to
`xengine.simulation = { path = ".../modules/base/simulation" }`; existing
`import mgame.sim;` code can remain. mcpp rejects the old dependency key
`mgame.sim` because its leaf name differs from `simulation`; source import
compatibility does not imply package-key or old-path compatibility.
