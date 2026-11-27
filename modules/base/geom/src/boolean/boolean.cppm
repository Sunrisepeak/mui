// mlibs.geom.boolean — solids combined (the foundation plan's §5.5: modelling by code).
//
//     const geom::Mesh block = geom::box({1, 1, 1});
//     const geom::Mesh bore = geom::cylinder(0.5f, 4, 48);
//     const auto drilled = geom::without(block, bore);          // the block with a hole through it — or why not
//     const auto both = geom::united(block, moved(block, {1, 0, 0}));
//
// The two meshes are solids: closed, each edge shared by two triangles, fronts outward (`closed` says whether one
// is, and why not). What comes back is a solid again — never a surface with a gap, an edge three faces share, or a
// part inside out — so a result can be combined again, measured (`volume`), or given to anything that asks for a
// closed mesh. Corners that are one place are joined first, so a mesh whose faces each have their own corners (a
// box's, for its flat normals) is a solid here.
//
// An attribute both meshes have — normals, tangents, texture coordinates — is carried: a surface keeps its own,
// and along the line where two surfaces now meet each has corners of its own there, so an edge the cut made is as
// sharp as it is. One only one of them has is left out.
//
// The same two meshes give the same bytes on every machine: the combining is exact where it decides and its
// arithmetic the same everywhere, and the corners and triangles are put in an order that is the result's own
// (by their numbers), not the order a sort happened to leave them in.
export module mlibs.geom.boolean;

import std;
export import mlibs.geom;

export namespace mlibs::geom {

enum class Combine : std::uint8_t {
    united,   // what is in either
    without,  // what is in the first and not in the second
    common,   // what is in both
};

// The two solids combined, or why they cannot be (one is not a mesh, or not a solid — which, and what is wrong).
// Nothing in common, or everything cut away: a mesh of no triangles.
[[nodiscard]] std::expected<Mesh, std::string> combined(const Mesh& one, const Mesh& other, Combine how);
[[nodiscard]] inline std::expected<Mesh, std::string> united(const Mesh& one, const Mesh& other) { return combined(one, other, Combine::united); }
[[nodiscard]] inline std::expected<Mesh, std::string> without(const Mesh& one, const Mesh& other) { return combined(one, other, Combine::without); }
[[nodiscard]] inline std::expected<Mesh, std::string> common(const Mesh& one, const Mesh& other) { return combined(one, other, Combine::common); }

// Whether a mesh is a solid — what `combined` takes — or why it is not.
[[nodiscard]] std::expected<void, std::string> closed(const Mesh& mesh);

}  // namespace mlibs::geom
