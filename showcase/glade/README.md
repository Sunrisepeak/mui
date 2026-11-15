# Glade

A pocket valley — a stream with a footbridge, grass on both banks, a campfire, a wooden crate, a block of ice — made of matter: a person or an agent pokes it with a few verbs (a torch held somewhere, water poured, a thing put down or pushed, the wind, rain, lightning) and the world answers by the laws. Its puzzles are problems the environment solves: set the crate across the stream alight without crossing it, light the campfire after the rain, melt the ice. Every answer can be asked why. M6.5's sandbox ([the design](../../.agents/docs/2026-11-15-m6.5-emergence-3d-design.md), T5.8).

**State of it**: the simulation (T5.8a) is in progress; the headless runtime and agent (T5.8b), the picture (T5.8c), the sound and the window (T5.8d–e) and the side-by-side comparison (T5.8f) follow.

| Package | What |
|---|---|
| [sim](sim) | The valley as carriers of matter in one step — bodies (`xengine.dynamics`' rigid3 and `Objects`), the air (`xengine.field`) and poured water (`xengine.particles`) under `rigid3::Scene` — with Glade's own materials as a pack of properties (no law); the verbs as quantities applied from outside and kept as causes; `observe`, `explain` and the task set; the state a value (a copy is a branch) |

```sh
mcpp test -p omui.showcase.glade.sim   # the same bits twice and on a branch; across, campfire and ice each solved, with the chain of causes
```
