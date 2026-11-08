// xengine.dynamics — every solved simulation (D-133): one owner for collision
// and the solver families (xpbd today; rigid, articulation, fluids, FEM, MPM
// as they come), a physical-material table they share, each family's stages
// stated. Stepped on mlibs.simulation's timeline; no renderer, no UI.
export module xengine.dynamics;
export import xengine.dynamics.collision;
export import xengine.dynamics.material;
export import xengine.dynamics.xpbd;
