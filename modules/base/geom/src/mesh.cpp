module mlibs.geom.api;

import std;
import mlibs.math;

namespace mlibs::geom {

namespace math = mlibs::math;

namespace {
bool number(float value) noexcept { return value == value && value - value == 0; }
bool numbers(Vec3 value) noexcept { return number(value.x) && number(value.y) && number(value.z); }
constexpr double turn = 6.283185307179586476925286766559;

void quad(Mesh& mesh, std::uint32_t first, std::uint32_t second, std::uint32_t third, std::uint32_t fourth) {
    for (const std::uint32_t index : {first, second, third, first, third, fourth}) mesh.indices.push_back(index);
}
}  // namespace

std::expected<void, std::string> check(const Mesh& mesh) {
    const std::size_t count = mesh.positions.size();
    if (!mesh.normals.empty() && mesh.normals.size() != count) return std::unexpected(std::format("{} normals for {} positions (one each, or none)", mesh.normals.size(), count));
    if (!mesh.tangents.empty() && mesh.tangents.size() != count) return std::unexpected(std::format("{} tangents for {} positions (one each, or none)", mesh.tangents.size(), count));
    if (!mesh.uvs.empty() && mesh.uvs.size() != count) return std::unexpected(std::format("{} uvs for {} positions (one each, or none)", mesh.uvs.size(), count));
    if (!mesh.second_uvs.empty() && mesh.second_uvs.size() != count) return std::unexpected(std::format("{} second uvs for {} positions (one each, or none)", mesh.second_uvs.size(), count));
    if (!mesh.colours.empty() && mesh.colours.size() != count) return std::unexpected(std::format("{} colours for {} positions (one each, or none)", mesh.colours.size(), count));
    if (mesh.indices.size() % 3) return std::unexpected(std::format("{} indices: three a triangle", mesh.indices.size()));
    for (std::size_t index = 0; index < mesh.indices.size(); ++index)
        if (mesh.indices[index] >= count) return std::unexpected(std::format("index {} names position {}, of {}", index, mesh.indices[index], count));
    for (std::size_t index = 0; index < count; ++index)
        if (!numbers(mesh.positions[index])) return std::unexpected(std::format("position {} is not a number", index));
    for (std::size_t index = 0; index < mesh.normals.size(); ++index)
        if (!numbers(mesh.normals[index])) return std::unexpected(std::format("normal {} is not a number", index));
    return {};
}

Box3 bounds(const Mesh& mesh) noexcept { return Box3::of(mesh.positions); }

void compute_normals(Mesh& mesh) {
    mesh.normals.assign(mesh.positions.size(), Vec3{});
    for (std::size_t at = 0; at + 2 < mesh.indices.size(); at += 3) {
        const std::uint32_t first = mesh.indices[at], second = mesh.indices[at + 1], third = mesh.indices[at + 2];
        if (first >= mesh.positions.size() || second >= mesh.positions.size() || third >= mesh.positions.size()) continue;
        const Vec3 face = math::cross(mesh.positions[second] - mesh.positions[first], mesh.positions[third] - mesh.positions[first]);
        mesh.normals[first] = mesh.normals[first] + face, mesh.normals[second] = mesh.normals[second] + face, mesh.normals[third] = mesh.normals[third] + face;
    }
    for (Vec3& normal : mesh.normals) normal = math::normalized(normal);
}

std::expected<ClusteredMesh, std::string> clustered(Mesh mesh, std::uint32_t triangles) {
    if (!triangles) return std::unexpected(std::string("a cluster has a triangle at least"));
    const std::size_t total = mesh.triangles();
    std::vector<std::uint32_t> runs;
    for (std::size_t first = 0; first < total; first += triangles) runs.push_back(static_cast<std::uint32_t>(std::min<std::size_t>(triangles, total - first)));
    return clustered(std::move(mesh), runs);
}

std::expected<ClusteredMesh, std::string> clustered(Mesh mesh, std::span<const std::uint32_t> runs) {
    if (auto checked = check(mesh); !checked) return std::unexpected(checked.error());
    std::size_t said = 0;
    for (const std::uint32_t run : runs) {
        if (!run) return std::unexpected(std::string("a cluster has a triangle at least"));
        said += run;
    }
    if (said != mesh.triangles()) return std::unexpected(std::format("the clusters hold {} triangles; the mesh has {}", said, mesh.triangles()));
    ClusteredMesh out;
    std::vector<Vec3> corners;
    std::size_t first = 0;
    for (const std::uint32_t run : runs) {
        Cluster cluster{.first_index = static_cast<std::uint32_t>(first * 3), .triangle_count = run};
        first += run;
        corners.clear();
        Vec3 facing{};
        for (std::uint32_t triangle = 0; triangle < cluster.triangle_count; ++triangle) {
            const std::size_t at = cluster.first_index + static_cast<std::size_t>(triangle) * 3;
            const Vec3 one = mesh.positions[mesh.indices[at]], two = mesh.positions[mesh.indices[at + 1]], three = mesh.positions[mesh.indices[at + 2]];
            corners.push_back(one), corners.push_back(two), corners.push_back(three);
            facing = facing + math::normalized(math::cross(two - one, three - one));
        }
        cluster.bounds = sphere_of(corners);
        // The cone: about the mean of the triangles' normals, as wide as the one farthest from it; said only where
        // they all face within a quarter turn of that mean (else there is nowhere the whole cluster faces away from).
        const Vec3 axis = math::normalized(facing);
        float least = 1;
        for (std::size_t corner = 0; corner + 2 < corners.size(); corner += 3) {
            const Vec3 normal = math::normalized(math::cross(corners[corner + 1] - corners[corner], corners[corner + 2] - corners[corner]));
            if (normal == Vec3{}) continue;  // a triangle of no area faces nowhere
            least = std::min(least, math::dot(axis, normal));
        }
        if (!(axis == Vec3{}) && least > 0.1f) {
            cluster.cone_axis = axis;
            cluster.cone_cutoff = std::sqrt(1 - least * least);
        }
        out.clusters.push_back(cluster);
    }
    out.mesh = std::move(mesh);
    return out;
}

Mesh box(Vec3 half) {
    Mesh mesh;
    // A face: its normal, and the two directions across it (right, up) with cross(right, up) = normal.
    struct Face { Vec3 normal, right, up; };
    const std::array<Face, 6> faces{{{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
                                     {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}}};
    for (const Face& face : faces) {
        const auto first = static_cast<std::uint32_t>(mesh.positions.size());
        const std::array<std::pair<float, float>, 4> corners{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
        for (const auto& [across, along] : corners) {
            mesh.positions.push_back(math::scaled(face.normal + face.right * across + face.up * along, half));
            mesh.normals.push_back(face.normal);
            mesh.tangents.push_back({face.right, 1.0f});
            mesh.uvs.push_back({(across + 1) * 0.5f, (1 - along) * 0.5f});
        }
        quad(mesh, first, first + 1, first + 2, first + 3);
    }
    return mesh;
}

Mesh sheet(float width, float depth, std::uint32_t across, std::uint32_t along) {
    Mesh mesh;
    across = std::max(across, 1u), along = std::max(along, 1u);
    for (std::uint32_t row = 0; row <= along; ++row)
        for (std::uint32_t column = 0; column <= across; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(across), v = static_cast<float>(row) / static_cast<float>(along);
            mesh.positions.push_back({(u - 0.5f) * width, 0, (v - 0.5f) * depth});
            mesh.normals.push_back({0, 1, 0});
            mesh.tangents.push_back({1, 0, 0, 1});
            mesh.uvs.push_back({u, v});
        }
    const std::uint32_t stride = across + 1;
    for (std::uint32_t row = 0; row < along; ++row)
        for (std::uint32_t column = 0; column < across; ++column) {
            const std::uint32_t corner = row * stride + column;
            quad(mesh, corner, corner + stride, corner + stride + 1, corner + 1);
        }
    return mesh;
}

Mesh sphere(float radius, std::uint32_t rings, std::uint32_t sectors) {
    Mesh mesh;
    rings = std::max(rings, 2u), sectors = std::max(sectors, 3u);
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const double down = turn * 0.5 * ring / rings;  // from the north pole
        // The poles exactly, and the seam's two columns at the same place bit for bit: the shape is closed.
        const float level = ring == 0 ? 1.0f : ring == rings ? -1.0f : static_cast<float>(math::det::cos(down));
        const float reach = ring == 0 || ring == rings ? 0.0f : static_cast<float>(math::det::sin(down));
        for (std::uint32_t sector = 0; sector <= sectors; ++sector) {
            const double around = turn * (sector % sectors) / sectors;
            const auto cosine = static_cast<float>(math::det::cos(around)), sine = static_cast<float>(math::det::sin(around));
            const Vec3 out{reach * cosine, level, -reach * sine};
            mesh.positions.push_back(out * radius);
            mesh.normals.push_back(out);
            mesh.tangents.push_back({-sine, 0, -cosine, 1});
            mesh.uvs.push_back({static_cast<float>(sector) / static_cast<float>(sectors), static_cast<float>(ring) / static_cast<float>(rings)});
        }
    }
    const std::uint32_t stride = sectors + 1;
    for (std::uint32_t ring = 0; ring < rings; ++ring)
        for (std::uint32_t sector = 0; sector < sectors; ++sector) {
            const std::uint32_t corner = ring * stride + sector;
            // At a pole one of a band's two triangles has no area: left out.
            if (ring != 0) for (const std::uint32_t index : {corner, corner + stride, corner + 1}) mesh.indices.push_back(index);
            if (ring + 1 != rings) for (const std::uint32_t index : {corner + 1, corner + stride, corner + stride + 1}) mesh.indices.push_back(index);
        }
    return mesh;
}

Mesh cylinder(float radius, float height, std::uint32_t sectors) {
    Mesh mesh;
    sectors = std::max(sectors, 3u);
    const float top = height * 0.5f;
    // The side: a seam's two columns share positions and differ in u.
    for (std::uint32_t sector = 0; sector <= sectors; ++sector) {
        const double around = turn * (sector % sectors) / sectors;
        const auto cosine = static_cast<float>(math::det::cos(around)), sine = static_cast<float>(math::det::sin(around));
        for (const float level : {top, -top}) {
            mesh.positions.push_back({radius * cosine, level, -radius * sine});
            mesh.normals.push_back({cosine, 0, -sine});
            mesh.tangents.push_back({-sine, 0, -cosine, 1});
            mesh.uvs.push_back({static_cast<float>(sector) / static_cast<float>(sectors), level > 0 ? 0.0f : 1.0f});
        }
    }
    for (std::uint32_t sector = 0; sector < sectors; ++sector) {
        const std::uint32_t corner = sector * 2;
        quad(mesh, corner, corner + 1, corner + 3, corner + 2);
    }
    // The caps: a fan about a middle each.
    for (const float side : {1.0f, -1.0f}) {
        const auto middle = static_cast<std::uint32_t>(mesh.positions.size());
        mesh.positions.push_back({0, top * side, 0}), mesh.normals.push_back({0, side, 0}), mesh.tangents.push_back({1, 0, 0, 1}), mesh.uvs.push_back({0.5f, 0.5f});
        for (std::uint32_t sector = 0; sector <= sectors; ++sector) {
            const double around = turn * (sector % sectors) / sectors;
            const auto cosine = static_cast<float>(math::det::cos(around)), sine = static_cast<float>(math::det::sin(around));
            mesh.positions.push_back({radius * cosine, top * side, -radius * sine});
            mesh.normals.push_back({0, side, 0});
            mesh.tangents.push_back({1, 0, 0, 1});
            mesh.uvs.push_back({0.5f + 0.5f * cosine, 0.5f + 0.5f * sine * side});
        }
        for (std::uint32_t sector = 0; sector < sectors; ++sector) {
            const std::uint32_t one = middle + 1 + sector, next = one + 1;
            if (side > 0) for (const std::uint32_t index : {middle, one, next}) mesh.indices.push_back(index);
            else for (const std::uint32_t index : {middle, next, one}) mesh.indices.push_back(index);
        }
    }
    return mesh;
}

}  // namespace mlibs::geom
