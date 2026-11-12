# field and matter — skill

How an Agent examines and tries a field of cells run by a prepared program (`xengine.cellular.agent`), with no window. The operations, their parameters and effects are in [field.ops.omui.ts](field.ops.omui.ts) (the subject, asked to describe itself at its endpoint, answers the same); this is the order to use them in.

## Before

1. Ask `subjects`: the subject `field` says what it can do (observation within a budget, control, time, preview, causes) and what it cannot (the source of what it puts out, history). Where the field is of matter a second subject, `matter`, is beside it: use that one for everything but time (below).
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

## A field of matter

Where the cells are matter (`xengine.cellular.matter.agent`; operations in [matter.ops.omui.ts](matter.ops.omui.ts)), speak of it as matter: materials by name, channels in their units, laws by name. Time stays the field's — `field.step`, `field.seek` — and every `matter` answer says the frame it is of.

1. `matter.describe` first: the materials, the channels with their units, the laws. Then one material (`material`) or one law (`law`) in full, when it matters. Do not guess a property: a law's condition is given as it is written.
2. Look: `matter.observe` over a region — cells of each material, each channel's least, most and mean in its unit, how many cells are in each named state (burning). One cell: `matter.at`, which also gives the properties its state has changed (`adjusted`: wet wood's ignition point).
3. Before changing anything, `matter.branch`: some steps ahead on a copy, a material placed or a quantity applied first — the materials of a region now and then, and what would happen, counted by name (`ignite`, `boil`, `freeze`). Compare two branches.
4. Change: `matter.place` (a region of a material, new) and `matter.apply` (a channel `set`, raised by `add` or lowered by `take`, in its unit). Each is a frame, recorded with who did it; `field.seek` to the frame before takes it back.
5. Why: `matter.explain` on the cell. Read `records` from the first (the latest): `what` happened, by which `law`, what the law `read`, and the neighbours' parts — who `gave` what it needed, who `took` it away, what was `beside` it when something ran short — each with its own `because`, back to an `applied` or `placed` with its `by`. `depth` says how far back (2 unless said). For something that did *not* happen, name the law: `judged` says whether it holds now, the comparison that `fails`, and the values read.
6. What happened while you were not looking: `matter.events` `since` the frame you last knew, of a `name`, in a `region`; with a `budget` the latest are kept and `more` says how many were left out.

Verifying: the same as above, and — a branch's `events` are the events of the same frames when they are stepped; what `matter.explain` says a law read is what `matter.describe` says the law compares; a record names an actor the session knows, never one a request claims.

## Not here

Which rule wrote a cell of a field of plain rules (a field of matter says which law, above). The record keeps the last so many things (`kept`); what is older is gone, and a seek back drops what came after. What the field looks like on screen is its presenter's ([present](present)) and the renderer's.
