// xengine.dynamics.matter — what a body is made of, from xengine.matter's table (D-165: the one table physics,
// chemistry and the lattice share). The package's feature `matter`: the solver families alone know no table —
// a body takes its parameters as numbers — and a consumer that has materials asks for this.
//
//     import xengine.dynamics.matter;
//     auto body = dyn::SoftBody::ring(outline, dyn::params_of(*table, *table->material("jelly"), area), ground);
export module xengine.dynamics.matter;

import std;
export import xengine.dynamics.xpbd;
export import xengine.matter;

export namespace xengine::dynamics {

// A soft body's parameters from a material: its mechanical columns — friction, restitution, compliance,
// shape_stiffness — and its mass, its density over water's (1000 kg/m³) times `area`. What the table does not
// say keeps `base`'s; a material that is not the plan's leaves `base` as it is.
[[nodiscard]] BodyParams params_of(const xengine::matter::Program& matter, xengine::matter::MaterialId material, double area = 1, BodyParams base = {}) noexcept;

}  // namespace xengine::dynamics
