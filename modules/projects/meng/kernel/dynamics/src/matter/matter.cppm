// meng.dynamics.matter — what a body is made of, from meng.matter's table (D-165: the one table physics,
// chemistry and the lattice share). The package's feature `matter`: the solver families alone know no table —
// a body takes its parameters as numbers — and a consumer that has materials asks for this.
//
//     import meng.dynamics.matter;
//     auto body = dyn::SoftBody::ring(outline, dyn::params_of(*table, *table->material("jelly"), area), ground);
export module meng.dynamics.matter;

import std;
export import mlibs.dynamics.xpbd;
export import meng.matter;

export namespace meng::dynamics {

// A soft body's parameters from a material: its mechanical columns — friction, restitution, compliance,
// shape_stiffness — and its mass, its density over water's (1000 kg/m³) times `area`. What the table does not
// say keeps `base`'s; a material that is not the plan's leaves `base` as it is.
[[nodiscard]] mlibs::dynamics::BodyParams params_of(const meng::matter::Program& matter, meng::matter::MaterialId material, double area = 1, mlibs::dynamics::BodyParams base = {}) noexcept;

}  // namespace meng::dynamics
