# air — skill

How an Agent examines and tries the air (`meng.field.agent`): its cells' warmth, wetness and charge and the wind that warm air makes, with no window. The operations, their parameters and effects are in [air.ops.xdef.ts](air.ops.xdef.ts) (the subject, asked to describe itself at its endpoint, answers the same); this is the order to use them in.

## Before

1. Ask `subjects`: the subject `air` says what it can do (observation within a budget, control, time, preview, causes) and what it cannot yet (the source of what it puts out, history).
2. `air.observe` gives the frame, the cells and parked blocks, the strongest wind and each channel over the field — least, most and where, total. A place is `[x, y, z]` in metres; y is up.

## Doing

1. Look first: `air.observe`, then `air.at` for one place: its cell (none where the air has no cells), each channel in its unit, how warm in kelvin, the wind.
2. Before applying anything, ask `air.branch` what would be some steps ahead with it applied (`set`): the future as `air.observe` gives it, the air itself untouched. Compare two branches rather than trying one and seeking back.
3. `air.set` applies a quantity to a place's cell from the next step: once, or `hold` it every step (a fire that keeps burning) until the next `air.set` of that cell and channel. Its answer says the frame from which the state shows it (`effective`).
4. `air.step` plays it on; warm air takes some steps to rise.
5. `air.explain` says why a place's cell is what it is after the last step: which laws held, what flowed in and out, by which law, from which neighbour's place.
6. To try otherwise, `air.seek` back to a frame within the history kept: what is stepped from there is a new branch, and what was held is let go.

## Verifying

- The same requests give the same hash, on every platform: run a sequence twice and compare.
- A branch never changes the air; its frame is the frame now plus its steps.
- Heat that arrived is said by `air.explain`'s flows (`heat flows`, in, from the neighbour's place), not guessed from two readings.
- A refusal says why (no cell at a place, a channel not the program's, steps out of range, a frame outside the history kept): do not repeat it.

## Not here

What a body or a particle in the air does with the wind and the heat is theirs (they join a step as guests). Why something elsewhere is as it is, across carriers, comes with the world's subject.
