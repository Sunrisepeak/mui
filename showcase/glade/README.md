# Glade

A pocket valley — a stream with a footbridge, grass on both banks, a campfire, a wooden crate, a block of ice — made of matter: a person or an agent pokes it with a few verbs (a torch held somewhere, water poured, a thing put down or pushed, the wind, rain, lightning) and the world answers by the laws. Its puzzles are problems the environment solves: set the crate across the stream alight without crossing it, light the campfire after the rain, melt the ice. Every answer can be asked why. M6.5's sandbox ([the design](../../.agents/docs/2026-11-15-m6.5-emergence-3d-design.md), T5.8).

**State of it**: the simulation (T5.8a) and the headless runtime with its agent (T5.8b) are done; the picture (T5.8c), the sound and the window (T5.8d–e) and the side-by-side comparison (T5.8f) follow.

| Package | What |
|---|---|
| [headless](headless) | The valley as a game on its own (D-162; T5.8b): xengine.runtime steps, seeks and branches it; a hand over it (the pointer aims; torch, pour, push, rain, lightning, a rod; the wind) is how a person or an agent pokes it; the view is the semantic projection with the goals, and shown everything, why each changed thing is what it became |
| [sim](sim) | The valley as carriers of matter in one step — bodies (`xengine.dynamics`' rigid3 and `Objects`), the air (`xengine.field`) and poured water (`xengine.particles`) under `rigid3::Scene` — with Glade's own materials as a pack of properties (no law); the verbs as quantities applied from outside and kept as causes; `observe`, `explain` and the task set; the state a value (a copy is a branch) |

```sh
mcpp test -p omui.showcase.glade.sim   # the same bits twice and on a branch; across, campfire and ice each solved, with the chain of causes
```

## Experimental (release 0.1, D-199)

- **Target**: emergence of the class of an open-world sandbox (side by side with a comparable scene of Breath of the Wild, T5.8f): fire, water, wind, ice and lightning answering by the laws, every answer explainable.
- **Now**: the simulation (T5.8a) — terrain, carriers in one state that is a value, the seven verbs as causes; the game at the runtime (T5.8b): an agent reading only the view meets three of the four goals by trying the hand on branches (3 branches), each answer back to the hand (`glade.headless`).
- **Gap**: magnitudes are not yet physical — a struck 56 kg iron rod cools in about 1 s where m·c/(h·A) is about 35 min, a 115 kg block of ice melts in 1.6 s by a campfire (R11: capacity by mass alone, every pair's weight 1, no radiation); burning has no energy balance; an answer that came through the air stops there (the air's own record is not linked into the chain); no picture, sound or window yet (T5.8c–e); the storm goal is not met (a struck rod's heat is R11's).
- **Limits**: one valley; CPU only.
- **Graduation to preview**: T5.8c–e done; the storm goal met; R11's magnitudes within a stated tolerance of hand calculation; one headless check on every tier-1 row.
