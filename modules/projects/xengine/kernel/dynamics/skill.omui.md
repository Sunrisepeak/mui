# body — skill

How an Agent examines and tries a soft body the XPBD family steps (`xengine.dynamics.agent`), with no window. The operations, their parameters and effects are in [body.ops.omui.ts](body.ops.omui.ts) (the subject, asked to describe itself at its endpoint, answers the same); this is the order to use them in.

## Before

1. Ask `subjects`: the subject `body` says what it can do (observation within a budget, control, time, preview) and what it cannot yet (causes, the source of what it puts out, history).
2. `body.observe` gives the frame, the centre, the bounds, the area against the rest area (its squash), the fastest particle's speed and what the last step touched. The page's y grows downwards.

## Doing

1. Look first: `body.observe`. A body at rest has contacts and a `fastest` near zero; one in the air has none. `body.contacts` says the same in words; `body.particles` gives every particle when the outline itself is in question.
2. Before pushing, ask `body.branch` what would be some steps ahead under a push: the future as `body.observe` gives it, the body itself untouched. Compare two branches rather than trying one and seeking back.
3. `body.push` holds a `force` and a `walk` from the next step on, until the next push; a `kick` is given once, on the next step alone. Its answer says the frame from which the state shows it (`effective`).
4. `body.step` plays it on; step past `effective` before judging what the push did.
5. To try otherwise, `body.seek` back to a frame within the history kept: what is stepped from there is a new branch, and what was held is let go.

## Verifying

- The same requests give the same hash, on every platform: run a sequence twice and compare.
- A branch's hash is the hash of the same steps played; `body.branch` never changes the body.
- Rest is `fastest` near zero with contacts over several steps, not one reading.
- A refusal says why (steps out of range, a frame outside the history kept): do not repeat it.

## Not here

Why the body is where it is as a record — who pushed it, which contact turned it — comes with the kernels' record of causes. What it looks like is its consumer's (hanim carries a figure on it) and the renderer's.
