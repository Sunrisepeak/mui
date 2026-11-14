// The one unit that names meshoptimizer (D-83).
module xrender.asset;

import std;
import mcpplibs.mwrap.meshoptimizer;

namespace xrender::asset {

namespace {

// An attribute's elements moved to their new places (`remap[old]` is the new place, or ~0u: no triangle uses it).
template <typename Element>
void moved(std::vector<Element>& elements, std::span<const unsigned int> remap, std::size_t kept) {
    if (elements.empty()) return;
    std::vector<Element> out(kept);
    meshopt_remapVertexBuffer(out.data(), elements.data(), elements.size(), sizeof(Element), remap.data());
    elements = std::move(out);
}

}  // namespace

std::expected<mlibs::geom::ClusteredMesh, std::string> optimised(mlibs::geom::Mesh mesh) {
    using mlibs::geom::Vec3;
    if (auto checked = mlibs::geom::check(mesh); !checked) return std::unexpected(checked.error());
    if (mesh.indices.empty()) return mlibs::geom::clustered(std::move(mesh), std::span<const std::uint32_t>{});
    static_assert(sizeof(Vec3) == 3 * sizeof(float) && sizeof(std::uint32_t) == sizeof(unsigned int));
    const std::size_t corners = mesh.positions.size(), count = mesh.indices.size();

    // The order a vertex cache serves best: what the meshlets are then cut from.
    meshopt_optimizeVertexCache(mesh.indices.data(), mesh.indices.data(), count, corners);

    // Meshlets: triangles that lie together and face alike, no more than a cluster holds. The builder that may
    // close a cluster early (a quarter full at least) where taking more would make it wide: the plain one fills
    // every cluster and gathers what is left at the end into ones that span the mesh (a sheet of 4,608 scattered
    // triangles: its widest cluster 4.04 across against 1.39 here, in as many clusters) — and a wide cluster is
    // one culling can never leave out.
    constexpr std::size_t most_corners = 255;
    const std::size_t most_triangles = mlibs::geom::most_cluster_triangles;
    const std::size_t least_triangles = most_triangles / 4;
    std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(count, most_corners, least_triangles));
    std::vector<unsigned int>    meshlet_corners(count);
    std::vector<unsigned char>   meshlet_triangles(count);
    meshlets.resize(meshopt_buildMeshletsFlex(meshlets.data(), meshlet_corners.data(), meshlet_triangles.data(), mesh.indices.data(), count, &mesh.positions[0].x, corners,
                                              sizeof(Vec3), most_corners, least_triangles, most_triangles, 0.25f, 2.0f));

    // The index buffer in the meshlets' order: a cluster is a run of it.
    std::vector<std::uint32_t> indices, runs;
    indices.reserve(count), runs.reserve(meshlets.size());
    for (const meshopt_Meshlet& meshlet : meshlets) {
        if (!meshlet.triangle_count) continue;
        for (std::size_t corner = 0; corner < std::size_t{meshlet.triangle_count} * 3; ++corner)
            indices.push_back(meshlet_corners[meshlet.vertex_offset + meshlet_triangles[meshlet.triangle_offset + corner]]);
        runs.push_back(meshlet.triangle_count);
    }
    if (indices.size() != count) return std::unexpected(std::format("the meshlets hold {} corners of triangles; the mesh has {}", indices.size(), count));

    // The corners in the order the clusters use them; one no triangle uses is left out.
    std::vector<unsigned int> remap(corners);
    const std::size_t kept = meshopt_optimizeVertexFetchRemap(remap.data(), indices.data(), count, corners);
    meshopt_remapIndexBuffer(indices.data(), indices.data(), count, remap.data());
    moved(mesh.positions, remap, kept), moved(mesh.normals, remap, kept), moved(mesh.tangents, remap, kept), moved(mesh.uvs, remap, kept), moved(mesh.second_uvs, remap, kept), moved(mesh.colours, remap, kept);
    moved(mesh.joints, remap, kept), moved(mesh.weights, remap, kept);
    mesh.indices = std::move(indices);
    return mlibs::geom::clustered(std::move(mesh), runs);
}

}  // namespace xrender::asset
