# material lab — skill

How an Agent designs matter in a lab (`xengine.matter.lab`), with no window: materials as rows, the laws, the look, a scene to try them in. A lab's session has four subjects — `lab` (the design: [lab.ops.omui.ts](lab.ops.omui.ts)), `matter` (what is there, why, what would be), `field` (time), `frame` (the picture); each, asked to describe itself, lists its operations. This is the order to use them in. For `matter` and `field` on their own, see [the lattice's skill](../../cellular/skill.omui.md).

## Before

1. `lab.describe`: the sources (a material a file, `laws.json`, `look.json`, `scene.json`), the world's shape and steps, the branch and commit shown, whether there are changes not committed, and the claims held. With others at the lab, read `claims` first.
2. `matter.describe` for what the definition holds — materials, channels with their units, laws as written. A property you are not sure of: read the material's row (`lab.read`), do not guess.
3. Working beside others: `lab.claim` the part you are about to change (`materials/<name>`, `laws/<name>`, `look`, `scene`), with an `intent` another can read. It is a lease: ask again to renew it, `lab.release` when done. A refusal with `conflict` says who holds the part and for how long — work on another part, or wait; do not retry in a loop.

## Doing

1. Set the scene with `matter.place` and `matter.apply`, run it with `field.step`. Every edit is kept in `scene.json` with its step and who made it: the scene is played again, edit by edit, whenever the definition changes. `lab.commit` keeps a scene worth returning to.
2. Before changing a row, `lab.try`: the scene played under the change and under what runs, some `steps` on — `differ` (cells whose material is another), `materials` and `happened`, each `[as it is, as it would be]`. Try two values rather than making one and reverting.
3. Change one thing at a time: `lab.material` (a row's properties; `null` takes one away; `look` is the row's colour), `lab.law` (one law by name), `lab.edit` (a source's whole text). The answer says what the change did to the world at the same step; `intent` becomes the commit's message — say what it is for.
4. A new material is a row: its properties, what it becomes (`burnt`, `melted`, `boiled`, `frozen`: other materials' names), a `look`. It needs no law — the laws read properties. If you find yourself writing a law that would name it, the property is missing: add the property.
5. Look: `frame.capture` (the PNG), `frame.at` (a pixel and the layer that drew it), `lab.picture` (a file kept by content, any scale). Ask `matter.explain` on a cell for why it is what it is.
6. Alternatives live on branches: `lab.branch`, `lab.switch` (commit first), `lab.merge` — by source and by line, so two materials added on two branches are both there. A merge that says both changed the same lines names the sources: agree on one side (`lab.diff` shows the lines), commit, merge again.

## Verifying

- A change that is refused changed nothing, and says where: the source and the place in it (`materials/coal.json /burnt`, `laws.json /4/when`, `scene.json /edits/2`).
- What `lab.try` says is what the change does: the same `materials`, the same `differ`.
- `lab.explain` on a part: the commits that changed it — who, what for, the lines; with `property`, the line that says it now and who wrote it.
- The same sources and the same steps are the same bits: `lab.describe`'s world hash on another machine, or after the directory is opened again.
- Taken back: `lab.revert` to `HEAD~1` (a new commit; nothing is lost).

## Not here

Time is the field's (`field.step`, `field.seek`; a seek back drops the scene's edits made after it). Definitions are JSON snapshots — what a `*.matter.omui.ts` evaluates to (`omui-gen snapshot`); the lab writes `world.matter.omui.json` and `world.appearance.omui.json` beside its sources for a world to load. Bodies (soft and rigid) are dynamics'; the lab's field is the lattice.
