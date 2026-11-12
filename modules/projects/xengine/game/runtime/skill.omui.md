# world — skill

How an Agent plays, tests or examines a game that runs on its own (`xengine.runtime`), with no window. The operations, their parameters and effects are in [world.ops.omui.ts](world.ops.omui.ts) (the subject, asked to describe itself at its endpoint, answers the same); this is the order to use them in.

## Before

1. Find the game: its endpoint is listed by name with the interface `xengine.world@0.1`. Ask `subjects`: the subject `world` says what it can do (observation, control, time, preview) and what it cannot yet (causes, history).
2. `world.state` says the frame, the state's hash, and whether the agent steps the game or its owner's clock does. `world.observe` says the tier you play on: `human` is shown what a person sees and acts as a person can; `assist` is privileged and is not a fair player.

## Doing

1. Look first: `world.observe` gives the frame and what a player is shown. Decide on that frame.
2. Before an action whose outcome matters, ask `world.branch` what would be some steps ahead holding it: the future's view, the game itself untouched. Compare two branches rather than trying one and seeking back.
3. Act with `world.act`, saying the frame you decided on (`seen`). Its answer says the frame from which the state shows it (`effective`): on the `human` tier no sooner than a tenth of a second after `seen`. An action holds, like a key, until the next; a press is a change from not held to held.
4. Where the agent steps the game, `world.step` plays it on; step past `effective` before judging what the action did. Where the owner's clock steps it, observe again instead.
5. To try otherwise, `world.seek` back to a frame within the history kept: what is played from there is a new branch, and what was held is let go.

## Verifying

- The same requests at the same frames give the same hash: play a sequence twice and compare `world.state`.
- A branch's hash is the hash of the same steps played; `world.branch` never changes `world.state`.
- A refused action says why (more often than the tier allows, a frame not yet played): slow down or observe again, do not repeat it.
- Record the frames, the actions and the hashes: they are the replay.

## Not here

What the game draws is its renderer's (xrender) and, in a window, omui's; why a state is what it is comes with the kernels' records of causes. A game shown in omui is played through the same actions (`xengine.ui`, the bridge).
