# particles — skill

How an Agent examines and tries a cloud of particles in 3D (`xengine.particles.agent`), with no window. The operations, their parameters and effects are in [particles.ops.omui.ts](particles.ops.omui.ts) (the subject, asked to describe itself at its endpoint, answers the same); this is the order to use them in.

## Before

1. Ask `subjects`: the subject `particles` says what it can do (observation within a budget, control, time, preview) and what it cannot yet (causes, the source of what it puts out, history).
2. `particles.observe` gives the frame, the fingerprint, how many are in flight by material, what struck the world's surfaces in the last step (fastest first) and how many things matter's laws did. Places are metres, `[x, y, z]` with y up.

## Doing

1. Look first: `particles.observe`, then `particles.list` (in the order emitted, a page at a time with `first` and `most`) or `particles.near` a place (the nearest first). Each particle says its material, place, velocity, age, life left and temperature in kelvin.
2. Before emitting, ask `particles.branch` with the same emission: the cloud some steps ahead, itself untouched. Compare branches rather than trying one and seeking back.
3. `particles.emit` gives particles of a material of the world's definition (`iron`, `stone`, `water` …) on the next step: where, how fast, how hot, how many, and a `spread` of their speeds — the same spread every time at the same frame. Its answer says the frame from which the cloud shows them (`effective`).
4. `particles.step` plays it on: they fly, strike the world's surfaces (bouncing by their material), cool or heat in the air and beside each other by matter's laws, and are gone when their life is over.
5. To try otherwise, `particles.seek` back to a frame within the history kept: what is stepped from there is a new branch.

## Verifying

- The same requests give the same fingerprint, on every platform: run a sequence twice and compare.
- A seek reaches a frame's fingerprint again exactly; a branch never changes the cloud.
- What struck the world is a step's output: read `particles.observe` after `particles.step`, and step past `effective` before judging an emission.
- A refusal says why (no such material, a count or a frame out of range) and what to ask next: do not repeat it.

## Not here

Why a particle is as it is as a record — which law cooled it, which strike turned it — comes with the kernels' record of causes. What it looks like is the world's look (`Look::shade()` over the cloud's state) drawn by `xrender.particle`; what a strike sounds like is the audio engine's.
