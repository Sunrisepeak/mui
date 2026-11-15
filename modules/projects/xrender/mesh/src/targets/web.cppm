// xrender.mesh:target — the Web build (wasm32-emscripten, D-84) runs no
// threads: a page served without cross-origin isolation has no
// SharedArrayBuffer to give them.
module xrender.mesh:target;

namespace xrender::mesh {

inline constexpr bool threads_here = false;

}  // namespace xrender::mesh
