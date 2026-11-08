# xengine.simulation

Soft bodies (XPBD) on `mlibs.simulation`'s timeline — until they join `xengine.dynamics` with collision and the other solver families (D-133 §7.1 S2). The fixed-step clock and the timeline moved to [mlibs.simulation](../../../base/simulation) (S1).

`xengine.simulation.bodies` is the particle and constraint solver animation and physics share (convergence plan §6.9, D-131): a `SoftBody` is a ring of particles held by its edges' lengths, its enclosed area and the shape it was made in (shape matching, optionally upright), stepped by small-step XPBD under gravity against half-plane colliders and one-way ledges, with restitution and friction, a walking speed at its feet, a push and a hop; a `Pin` is a dragging hand. `SoftBodyModel` is a `mlibs::simulation` model, so a body runs in a `Timeline` with checkpoints and exact replay; stepping allocates nothing. hanim's `Engine::simulate` makes any part of any object such a body and carries its geometry on a cage over the ring. `tests/bodies_test.cpp` drops a ring on the ground and on ledges, drags one, walks one, keeps one upright, and replays it bit for bit.

| Optional package | Role |
|---|---|
| [cellular](cellular) | Prepared integer channel/neighborhood rules, immutable input and tiled output (to be renamed `xengine.cellular`, S3) |

Run `mcpp test -p xengine.simulation`.
