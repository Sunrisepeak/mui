// mlibs.dynamics — the generic solver core (D-133, D-231): the shapes a solver
// collides with and the queries on them, and the XPBD family of soft bodies,
// each family's stages stated. Stepped on mlibs.simulation's timeline; no
// renderer, no UI, no materials (those are the engine's meng.dynamics).
export module mlibs.dynamics;
export import mlibs.dynamics.collision;
export import mlibs.dynamics.xpbd;
export import mlibs.dynamics.inspect;
