// mlibs.geom.api:stored — a mesh in clusters as bytes: what is written to a file and read back (the foundation
// plan's §9.4: the stored form carries a version and is in clusters from the first day).
//
//     const std::vector<std::uint8_t> bytes = geom::stored(*geom::clustered(geom::sphere(1)));
//     const auto mesh = geom::read_stored(bytes);          // the same mesh, bit for bit — or what is wrong
//
// The bytes, every number little-endian:
//
//     "OMSH"   u32 version (1)   u32 sections   u32 0
//     a section each:  u32 kind   u32 bytes an element   u64 where it starts   u64 how many elements
//     the sections' elements
//
//     kind 1 positions (3 × f32)   2 normals (3 × f32)   3 tangents (4 × f32)   4 uvs (2 × f32)   5 indices (u32)
//          6 clusters (first index u32, triangles u32, bounds centre 3 × f32 and radius f32, cone axis 3 × f32 and
//            cutoff f32, error f32, parent's error f32, level u32)
//
// A reader passes over a section of a kind it does not know, so what is added later (a hierarchy's groups, skin
// weights, a second set of uvs) is a new kind and old readers still read the rest. A file of a later version is
// refused, with both numbers. Nothing is trusted: every count and place is checked against the bytes there are,
// and the mesh read is held to `check`.
export module mlibs.geom.api:stored;

import std;
import :mesh;

export namespace mlibs::geom {

inline constexpr std::array<std::uint8_t, 4> stored_magic{'O', 'M', 'S', 'H'};

// The mesh as bytes: the same mesh, the same bytes, everywhere.
[[nodiscard]] std::vector<std::uint8_t> stored(const ClusteredMesh& mesh);
// The mesh the bytes hold, or what is wrong with them.
[[nodiscard]] std::expected<ClusteredMesh, std::string> read_stored(std::span<const std::uint8_t> bytes);

}  // namespace mlibs::geom
