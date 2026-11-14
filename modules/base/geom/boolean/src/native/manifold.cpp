// The one unit that names Manifold (D-83).
module mlibs.geom.boolean;

import std;
import mlibs.math;
import mcpplibs.mwrap.manifold;

namespace mlibs::geom {

namespace {

using Solid = manifold::Manifold;

// Which attributes ride along, and how many numbers a corner is.
struct Carried {
    bool normals = false, tangents = false, uvs = false;
    [[nodiscard]] std::uint32_t numbers() const noexcept { return 3u + (normals ? 3u : 0u) + (tangents ? 4u : 0u) + (uvs ? 2u : 0u); }
};

std::string_view wrong(Solid::Error error) noexcept {
    switch (error) {
        case Solid::Error::NonFiniteVertex: return "a corner is not a number";
        case Solid::Error::NotManifold: return "it is not closed, or an edge is shared by other than two triangles facing the same way out";
        case Solid::Error::VertexOutOfBounds: return "an index names no corner";
        case Solid::Error::ResultTooLarge: return "too many triangles";
        default: return "it cannot be made a solid";
    }
}

std::expected<Solid, std::string> solid_of(const Mesh& mesh, Carried carried, std::string_view which) {
    if (auto checked = check(mesh); !checked) return std::unexpected(std::format("{} is not a mesh: {}", which, checked.error()));
    if (mesh.indices.empty()) return Solid();
    manifold::MeshGL given;
    given.numProp = carried.numbers();
    given.vertProperties.reserve(mesh.positions.size() * given.numProp);
    for (std::size_t corner = 0; corner < mesh.positions.size(); ++corner) {
        const Vec3 at = mesh.positions[corner];
        given.vertProperties.insert(given.vertProperties.end(), {at.x, at.y, at.z});
        if (carried.normals) given.vertProperties.insert(given.vertProperties.end(), {mesh.normals[corner].x, mesh.normals[corner].y, mesh.normals[corner].z});
        if (carried.tangents) given.vertProperties.insert(given.vertProperties.end(), {mesh.tangents[corner].x, mesh.tangents[corner].y, mesh.tangents[corner].z, mesh.tangents[corner].w});
        if (carried.uvs) given.vertProperties.insert(given.vertProperties.end(), {mesh.uvs[corner].x, mesh.uvs[corner].y});
    }
    given.triVerts.assign(mesh.indices.begin(), mesh.indices.end());
    given.Merge();  // corners at one place are one corner of the solid, whatever else they carry
    Solid solid(given);
    if (solid.Status() != Solid::Error::NoError) return std::unexpected(std::format("{} is not a solid: {}", which, wrong(solid.Status())));
    return solid;
}

// The solid as a mesh in the order that is its own: corners by their numbers, each triangle from its least corner,
// the triangles by theirs; corners that are the same in every number are one.
Mesh mesh_of(const Solid& solid, Carried carried) {
    const manifold::MeshGL made = solid.GetMeshGL();
    const std::size_t numbers = made.numProp, corners = numbers ? made.vertProperties.size() / numbers : 0;
    Mesh out;
    if (!corners || made.triVerts.empty()) return out;
    // Every corner as it will be stored: what was mixed along a new edge is made a direction again.
    std::vector<float> stored(made.vertProperties);
    const std::size_t normal_at = 3, tangent_at = normal_at + (carried.normals ? 3 : 0);
    if (numbers == carried.numbers())
        for (std::size_t corner = 0; corner < corners; ++corner) {
            float* numbers_of = stored.data() + corner * numbers;
            if (carried.normals) {
                const Vec3 normal = mlibs::math::normalized(Vec3{numbers_of[normal_at], numbers_of[normal_at + 1], numbers_of[normal_at + 2]});
                numbers_of[normal_at] = normal.x, numbers_of[normal_at + 1] = normal.y, numbers_of[normal_at + 2] = normal.z;
            }
            if (carried.tangents) {
                const Vec3 along = mlibs::math::normalized(Vec3{numbers_of[tangent_at], numbers_of[tangent_at + 1], numbers_of[tangent_at + 2]});
                numbers_of[tangent_at] = along.x, numbers_of[tangent_at + 1] = along.y, numbers_of[tangent_at + 2] = along.z;
                numbers_of[tangent_at + 3] = numbers_of[tangent_at + 3] < 0 ? -1.0f : 1.0f;
            }
        }
    else carried = {};  // not the numbers asked for: only the places are taken
    if (numbers < 3) return out;
    const auto key = [&](std::uint32_t corner) { return std::span<const float>(stored.data() + std::size_t{corner} * numbers, numbers); };
    const auto before = [&](std::uint32_t one, std::uint32_t other) {
        return std::ranges::lexicographical_compare(key(one), key(other), {}, [](float value) { return std::bit_cast<std::uint32_t>(value); }, [](float value) { return std::bit_cast<std::uint32_t>(value); });
    };
    std::vector<std::uint32_t> order(corners);
    std::ranges::iota(order, 0u);
    std::ranges::sort(order, before);
    std::vector<std::uint32_t> place(corners);
    std::uint32_t kept = 0;
    for (std::size_t index = 0; index < order.size(); ++index) {
        const bool same = index > 0 && !before(order[index - 1], order[index]);
        if (!same) {
            const std::span<const float> from = key(order[index]);
            out.positions.push_back({from[0], from[1], from[2]});
            std::size_t at = 3;
            if (carried.normals) out.normals.push_back({from[at], from[at + 1], from[at + 2]}), at += 3;
            if (carried.tangents) out.tangents.push_back({from[at], from[at + 1], from[at + 2], from[at + 3]}), at += 4;
            if (carried.uvs) out.uvs.push_back({from[at], from[at + 1]});
            ++kept;
        }
        place[order[index]] = kept - 1;
    }
    std::vector<std::array<std::uint32_t, 3>> triangles;
    triangles.reserve(made.triVerts.size() / 3);
    for (std::size_t at = 0; at + 2 < made.triVerts.size(); at += 3) {
        std::array<std::uint32_t, 3> triangle{place[made.triVerts[at]], place[made.triVerts[at + 1]], place[made.triVerts[at + 2]]};
        std::ranges::rotate(triangle, std::ranges::min_element(triangle));
        triangles.push_back(triangle);
    }
    std::ranges::sort(triangles);
    out.indices.reserve(triangles.size() * 3);
    for (const std::array<std::uint32_t, 3>& triangle : triangles) out.indices.insert(out.indices.end(), triangle.begin(), triangle.end());
    return out;
}

}  // namespace

std::expected<Mesh, std::string> combined(const Mesh& one, const Mesh& other, Combine how) {
    // An attribute rides along where both have it (a mesh of no triangles has whatever the other has).
    const auto has = [&](auto member) {
        const bool first = one.indices.empty() || !(one.*member).empty(), second = other.indices.empty() || !(other.*member).empty();
        return first && second && !(one.indices.empty() && other.indices.empty());
    };
    const Carried carried{.normals = has(&Mesh::normals), .tangents = has(&Mesh::tangents), .uvs = has(&Mesh::uvs)};
    const auto first = solid_of(one, carried, "the first");
    if (!first) return std::unexpected(first.error());
    const auto second = solid_of(other, carried, "the second");
    if (!second) return std::unexpected(second.error());
    const Solid result = first->Boolean(*second, how == Combine::united ? manifold::OpType::Add : how == Combine::without ? manifold::OpType::Subtract : manifold::OpType::Intersect);
    if (result.Status() != Solid::Error::NoError) return std::unexpected(std::format("they cannot be combined: {}", wrong(result.Status())));
    return mesh_of(result, carried);
}

std::expected<void, std::string> closed(const Mesh& mesh) {
    if (mesh.indices.empty()) return std::unexpected(std::string("a mesh of no triangles holds no space"));
    const auto solid = solid_of(mesh, {}, "it");
    if (!solid) return std::unexpected(solid.error());
    return {};
}

}  // namespace mlibs::geom
