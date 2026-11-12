// xengine.dynamics — every solved simulation (D-133): one owner for collision
// and the solver families (xpbd today; rigid, articulation, fluids, FEM, MPM
// as they come), each family's stages stated; what a body is made of is
// xengine.matter's table (its mechanical columns). Stepped on mlibs.simulation's timeline; no renderer, no UI.
export module xengine.dynamics;
export import xengine.dynamics.collision;
export import xengine.dynamics.xpbd;
export import xengine.dynamics.inspect;
