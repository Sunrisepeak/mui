# Glade sim

A pocket valley made of matter, poked by a few verbs, answering by the laws: the simulation of [Glade](../../..) (the M6.5 design's T5.8a). Ground of heights (flat banks, the stream's channel, a hill), a stream with a footbridge, grass on both banks, a campfire of logs and kindling in a ring of stones, a wooden crate in the long grass at the far bank's end, a block of ice, a tree on each bank.

```cpp
import omui.showcase.glade.sim;

auto state = glade::start(glade::level::glade());
glade::torch(state, {-3, 0.15f, 0}, 1);   // a torch held to the near bank's grass for a second
glade::run(state, 1200);                  // twenty seconds
glade::explain(state, "crate");           // why it burns: back through the far grass, the footbridge, the near grass, to the torch
```

- **Carriers in one step.** Every thing is a body of `xengine.dynamics`' rigid3 world and an instance of matter (`rigid3::Objects`); the air is a field (`xengine.field`) with its wind; poured water is particles that take room (`xengine.particles`); all of it is one tick of matter (`rigid3::Scene`). Nothing here says what fire, water or wind do: the standard laws do, with Glade's own materials — [glade.matter.xdef.ts](glade.matter.xdef.ts), grass, dry leaves and bark as rows of properties (a pack, no law).
- **Verbs.** `torch` (the nearest carried thing held hot), `pour` (a bucket's drops), `drop` (a thing put down), `push`, `wind`, `rain`, `strike` (lightning on the highest open thing that conducts). Each is a quantity applied from outside, kept as a cause; `done` says each in words with its time.
- **Seen and said.** `observe` is the semantic projection — each thing's material, warmth, whether it burns, how wet, whether it is open to the sky — and `explain` the chain of causes in words, across bodies and drops (`matter::Causes`); asked of something burning, it says why it burns.
- **A value.** The rigid world is held by `mlibs::simulation::Branching`, so a state copied is a branch: what an agent tries a verb on first. `checksum` is every carrier's hash.
- **Tasks.** Problems the environment solves, each a goal, the verbs it may use and where they may be done: `across` (the crate on the far bank alight without crossing the stream), `campfire` (lit after the rain), `ice` (melted), `storm` (the campfire lit with no torch: lightning drawn by a rod; waiting on the kernel's heat by surface and mass).

## Agent surface

The state is the subject (T5.8b): `observe` and `explain` are what an agent reads, the verbs what it does, a copy what it tries them on — each answer with its causes. Its operations, `describe` and the task set come with the headless runtime.

```sh
mcpp test -p omui.showcase.glade.headless
```
