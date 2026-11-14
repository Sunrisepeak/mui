# nav — skill

How an Agent examines and tries a navigation world (`xengine.nav.agent`) with no window: a mesh built from triangles, paths across it, a crowd walking it. The operations, their parameters and effects are in [nav.ops.omui.ts](nav.ops.omui.ts) (the subject, asked to describe itself at its endpoint, answers the same); this is the order to use them in.

## Before

1. Ask `subjects`: the subject `nav` says what it can do (observation within a budget, control, time, preview) and what it cannot yet (causes, the source of what it puts out, history).
2. `nav.observe` gives the frame, the mesh's tiles and polygons, how many walkers are going which way, and how many obstacles and links there are. A place is metres, `[x, y, z]` with y up.

## Doing

1. Ask the way before sending anyone: `nav.path` says whether it reaches (or ends as near as it can), its corners and its length; `nav.ray` whether a straight walk gets there and where it stops. A place off the mesh is refused: `nav.nearest` finds the mesh near it.
2. `nav.add` a walker, then `nav.target` it: its path is planned whole at once, and the answer says how it is going (`walking`, or `unreachable` when it can only get near).
3. `nav.step` plays the crowd on; `nav.where` reads each walker's place, how it is going and the corners it steers for next (within a `budget` for many walkers). `arrived` is within its radius of the target.
4. To change the ground, `nav.obstacle` (a cylinder or a box; `remove` takes one away) or `nav.link` (a jump or a ladder between two places; `remove` too): only the tiles they touch are built again, and walkers whose way they cross plan again in the next step. Ask `nav.path` again to see the effect.
5. Before changing anything, `nav.branch` says what would be some steps ahead (a walker sent first if one is given), the world itself untouched. To try otherwise after the fact, `nav.seek` back to a frame within the history kept.
6. `nav.build` replaces the whole mesh from triangles (and a walker size); the old obstacles, links and walkers go with it.

## Verifying

- The same requests give the same hash: run a sequence twice and compare; a branch's hash is the hash of the same steps played.
- A walker across a link reads `crossing`; one whose ground was built away reads `off_mesh` and waits.
- A refusal says why and what to ask instead (a walker or obstacle that is not there, a place off the mesh, steps out of range): do not repeat it.

## Not here

Why a walker went where it went as a record — who sent it, which walker made it step aside, why its path was planned again — comes with the kernels' record of causes. Bodies of a 3D rigid world are not seen by the crowd unless placed here as obstacles.
