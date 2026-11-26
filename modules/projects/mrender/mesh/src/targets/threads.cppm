// mrender.mesh:target (internal) — what the target can do, as a constant chosen by the
// manifest per target (no macro). Here: a target that runs threads.
module mrender.mesh:target;

namespace mrender::mesh {

inline constexpr bool threads_here = true;

}  // namespace mrender::mesh
