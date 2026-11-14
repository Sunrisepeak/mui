# skeleton — skill

How an Agent examines and directs skeletal characters (`hanim.skeleton.agent`) with no window: a rig (a skeleton and its clips), characters whose state machines choose what plays, layers on top, joints in the world. The operations, their parameters and effects are in [skeleton.ops.omui.ts](skeleton.ops.omui.ts) (the subject, asked to describe itself at its endpoint, answers the same); this is the order to use them in.

## Before

1. Ask `subjects`: the subject `skeleton` says what it can do (observation within a budget, control, time, preview) and what it cannot yet (causes as a record, the source of what it puts out, history).
2. `skeleton.observe` gives the frame, the rig — its joints, its clips (length, raw and compressed bytes, the error compression left, whether the clip carries root motion), the machine's states and parameters — and each character: its state, how far the fade into it is, what plays with which weights, the last transition and the conditions it asked, its parameters, layers and where it stands. A place is metres, `[x, y, z]` with y up; a character is its index from 0.

## Doing

1. To see a clip without playing it, `skeleton.sample` any clip at any time: a joint's place in the model (or every joint's within a `budget`), the root motion from the clip's start, and the distance compression left there.
2. Let the machine choose: `skeleton.set` a parameter (`speed`; a trigger such as `halt` is set to 1 and taken by the transition that reads it). Its transitions are read from the next step.
3. Or choose for it: `skeleton.play` a state with a `fade` in seconds; what was playing fades out keeping its shares (a fade interrupted carries on from where it was).
4. `skeleton.blend` layers a clip over what the machine plays: mixed in on a joint's subtree (`mask`, e.g. `upper_chest` for a wave while walking) or `additive` (its difference from its own start, e.g. a lean). A `weight` of 0 takes the layer away.
5. `skeleton.step` plays every character on (1/60 s a step); root motion carries them. `skeleton.joint` says where a joint is in the world and in the model.
6. Before changing anything, `skeleton.branch` says what would be some steps ahead (a character sent to a state first if one is given), the characters themselves untouched. To try otherwise after the fact, `skeleton.seek` back to a frame within the history kept.

## Verifying

- The same requests give the same hash: run a sequence twice and compare; a branch's hash is the hash of the same steps played.
- Through a fade the weights of what plays sum to 1, and a joint moves no further in a step than the clips move it.
- A refusal says why and what to ask instead (a character, clip, state, parameter or joint that is not there, steps out of range): do not repeat it.

## Not here

Why a parameter changed — which agent or game set it — as a record comes with the kernels' record of causes; `observe` gives the last transition and its conditions. A character does not see the world: what it stands on and walks into are its owner's (a character controller, a navigation crowd), which places it.
