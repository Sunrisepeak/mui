// meng.dynamics — the engine's solved simulation (D-133, D-231): it builds
// on the foundation's solver core (mlibs.dynamics: collision, XPBD soft
// bodies), re-exported here, and adds the engine's families as features (rigid,
// rigid3; articulation, fluids, FEM, MPM as they come), what a body is made of
// from meng.matter's table (`matter`) and a body as an agent's subject
// (`agent`). Stepped on mlibs.simulation's timeline; no renderer, no UI.
export module meng.dynamics;
export import mlibs.dynamics;
