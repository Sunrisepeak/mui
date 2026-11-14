// mlibs.geom.api:mesh — a triangle mesh as data, stored in clusters, and the first shapes made by code.
//
//     geom::Mesh ball = geom::sphere(1.0f, 16, 32);
//     const auto stored = geom::clustered(std::move(ball));      // clusters of up to 124 triangles, each with bounds
//     for (const geom::Cluster& part : stored->clusters) if (seen.holds(part.bounds)) draw(part);
//
// A mesh's triangles are counter-clockwise seen from their front; a normal points out of the front; a tangent is
// (x, y, z, w) with w ±1 the side its bitangent is on (glTF's: bitangent = cross(normal, tangent.xyz) × w).
//
// The stored form is in clusters from the first day and carries a version (the foundation plan's §9.4): a cluster
// is a run of the index buffer with a bounding sphere and the cone its triangles face within — what culling reads
// now — and the two errors and the level a hierarchy of clusters would fill, which today are a single level's.
// `clustered` cuts the triangles in the order they are in; putting near triangles together first is an optimiser's
// work (to be joined under this form), which changes how good the clusters are and not what they are.
export module mlibs.geom.api:mesh;

import std;
import mlibs.math;
import :solid;

export namespace mlibs::geom {

using mlibs::math::Vec2;
using mlibs::math::Vec4;
using mlibs::math::Mat4;

struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;    // none, or one a position
    std::vector<Vec4> tangents;   // none, or one a position
    std::vector<Vec2> uvs;        // none, or one a position
    // A second set of texture coordinates, for pictures laid another way than the first (glTF's TEXCOORD_1: a
    // shade picture over a whole building while its colour repeats): none, or one a position.
    std::vector<Vec2> second_uvs;
    // A colour at each corner, linear (red, green, blue, and how much it hides), that the surface's own colour
    // and cover are multiplied by between them (glTF's COLOR_0): none, or one a position.
    std::vector<Vec4> colours;
    // What bends it (a skin): each position's four joints, by their numbers in the skin, and how much each moves
    // it (glTF's JOINTS_0 and WEIGHTS_0) — both, or neither; one a position. A renderer that is given the skin's
    // matrices bends the positions, normals and tangents by them (xrender.mesh's `bend`).
    std::vector<std::array<std::uint16_t, 4>> joints;
    std::vector<Vec4>                         weights;
    std::vector<std::uint32_t> indices;  // three a triangle
    [[nodiscard]] std::size_t triangles() const noexcept { return indices.size() / 3; }
};
// What is wrong with a mesh, or nothing: its attributes one a position or absent, its indices three a triangle and
// each naming a position, every number a number.
[[nodiscard]] std::expected<void, std::string> check(const Mesh& mesh);
// `from` put into `into` at `place`: its positions moved, its normals turned as a surface is (the inverse's
// transpose), its tangents' directions turned, everything else as it is, its triangles after `into`'s. A level of
// parts made one mesh. Refused (and `into` unchanged) where one has a kind of corner data the other has not (an
// empty `into` takes `from`'s kinds), or `place` cannot be undone.
[[nodiscard]] std::expected<void, std::string> append(Mesh& into, const Mesh& from, const Mat4& place);
[[nodiscard]] Box3 bounds(const Mesh& mesh) noexcept;
// Normals from the triangles: each position's the sum of its triangles' (so weighted by their area), of length 1.
void compute_normals(Mesh& mesh);

struct Cluster {
    std::uint32_t first_index = 0;      // into the mesh's indices
    std::uint32_t triangle_count = 0;
    Sphere bounds;
    // Every triangle of it faces within this cone about `cone_axis`. From an eye, the whole cluster faces away
    // where dot(direction from the eye to bounds.centre, cone_axis) ≥ cone_cutoff + bounds.radius / their distance
    // (`faces_away`). A cutoff above 1: no such place — its triangles face too many ways.
    Vec3 cone_axis{0, 0, 1};
    float cone_cutoff = 2;
    // For a hierarchy of clusters (not built today: one level, no error, no parent): the error of this cluster
    // against the mesh it was simplified from, its parent group's, and its level.
    float error = 0;
    float parent_error = std::numeric_limits<float>::infinity();
    std::uint32_t level = 0;
};
struct ClusteredMesh {
    static constexpr std::uint32_t version = 1;
    Mesh mesh;
    std::vector<Cluster> clusters;
};
// True only where no triangle of the cluster shows its front to `eye`: it need not be drawn for a view from there.
[[nodiscard]] inline bool faces_away(const Cluster& cluster, Vec3 eye) noexcept {
    if (cluster.cone_cutoff > 1) return false;
    const Vec3 to = cluster.bounds.centre - eye;
    const float distance = mlibs::math::length(to);
    if (!(distance > cluster.bounds.radius)) return false;  // the eye is inside its bounds
    return mlibs::math::dot(to, cluster.cone_axis) >= (cluster.cone_cutoff + cluster.bounds.radius / distance) * distance;
}
inline constexpr std::uint32_t most_cluster_triangles = 124;
// The mesh in clusters of at most `triangles` triangles each, in the order its triangles are in. Refused where
// `check` refuses it, or `triangles` is 0.
[[nodiscard]] std::expected<ClusteredMesh, std::string> clustered(Mesh mesh, std::uint32_t triangles = most_cluster_triangles);
// Each cluster's bounds and cone worked out again from where its triangles are now, as `clustered` works them out:
// after its positions have moved (a skin bent them) and its triangles have not. `corners` is room it reuses.
void refit(ClusteredMesh& mesh, std::vector<Vec3>& corners);
// The mesh in clusters of the sizes given, in the order its triangles are in: `runs[n]` triangles are cluster n.
// For an optimiser that has put the triangles of a cluster together (meshlets: `xrender.asset`'s `optimised`);
// the bounds and the cone are worked out here as above, so a cluster is the same thing whoever cut it. Refused
// where `check` refuses the mesh, a run is 0, or the runs are not all of its triangles.
[[nodiscard]] std::expected<ClusteredMesh, std::string> clustered(Mesh mesh, std::span<const std::uint32_t> runs);

// ── shapes made by code: positions, normals, tangents, uvs; the same bits everywhere ──
// A box about the origin.
[[nodiscard]] Mesh box(Vec3 half);
// A flat sheet in the x–z plane facing +Y, `across` × `along` quads.
[[nodiscard]] Mesh sheet(float width, float depth, std::uint32_t across = 1, std::uint32_t along = 1);
// A sphere about the origin: `rings` bands pole to pole (2 at least), `sectors` around (3 at least).
[[nodiscard]] Mesh sphere(float radius, std::uint32_t rings = 16, std::uint32_t sectors = 32);
// A cylinder about the y axis, capped, `sectors` around (3 at least).
[[nodiscard]] Mesh cylinder(float radius, float height, std::uint32_t sectors = 32);

}  // namespace mlibs::geom
