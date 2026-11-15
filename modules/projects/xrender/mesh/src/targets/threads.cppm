// xrender.mesh:target (internal) — what the target can do, as a constant chosen by the
// manifest per target (no macro). Here: a target that runs threads.
module xrender.mesh:target;

namespace xrender::mesh {

inline constexpr bool threads_here = true;

}  // namespace xrender::mesh
