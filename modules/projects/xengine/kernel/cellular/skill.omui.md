# field — skill

How an Agent examines and tries a field of cells run by a prepared program (`xengine.cellular.agent`), with no window. The operations, their parameters and effects are in [field.ops.omui.ts](field.ops.omui.ts) (the subject, asked to describe itself at its endpoint, answers the same); this is the order to use them in.

## Before

1. Ask `subjects`: the subject `field` says what it can do (observation within a budget, control, time, preview) and what it cannot yet (causes, the source of what it puts out, history).
2. `field.observe` with no region gives the shape, the frame, the steps run and each channel as numbers. What a channel means (a material, a temperature, a count) is the program's: read its owner's notes, do not guess from values.

## Doing

1. Read numbers before cells: `field.observe` over the region in question — least, most, sum, cells not zero — tells where something is. Narrow the region, then `field.cells` for the few cells that matter (4096 at most a request). With many channels, give a `budget`: channels are left out from the last and `more` says how many.
2. Before changing the field, ask `field.branch` what would be: a value placed and some steps run on a copy, observed over the region you care about. Compare two branches rather than trying one and seeking back.
3. `field.place` sets one channel over a region. It is a frame of its own; `field.step` runs the program. Their answers give the frame and the hash.
4. To try otherwise, `field.seek` back to a frame within the history kept (`earliest` … `latest`): what is run from there is a new branch.

## Verifying

- The same requests give the same hash: run a sequence twice and compare.
- A branch's hash is the hash of the same frames played; `field.branch` never changes the field.
- What must be conserved (a material's sum) is checked with `field.observe`'s `sum` before and after.
- A refusal says why (a region outside the field, more cells than a request reads, a budget below the least answer): ask for less, do not repeat it.

## Not here

Why a cell holds what it holds, and which rule wrote it, come with the matter kernel's record of causes. What the field looks like on screen is its presenter's ([present](present)) and the renderer's.
