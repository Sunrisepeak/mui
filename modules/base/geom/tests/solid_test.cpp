// mlibs.geom in three dimensions (the foundation plan's M6.3): boxes, spheres, rays, a view's frustum; a mesh, its
// stored form in clusters, the shapes made by code — each held by what must be true of it, and its bits pinned.
import std;
import mlibs.geom;
import mlibs.math;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace geom = mlibs::geom;
namespace math = mlibs::math;
using math::Vec3;

namespace {
bool near_to(float one, float other, float within = 1e-4f) { return std::abs(one - other) <= within; }
std::uint64_t bits(const geom::Mesh& mesh) {
    mlibs::hash::Fnv1a hash;
    for (const Vec3 value : mesh.positions) hash.f32(value.x).f32(value.y).f32(value.z);
    for (const Vec3 value : mesh.normals) hash.f32(value.x).f32(value.y).f32(value.z);
    for (const auto& value : mesh.tangents) hash.f32(value.x).f32(value.y).f32(value.z).f32(value.w);
    for (const auto& value : mesh.uvs) hash.f32(value.x).f32(value.y);
    for (const std::uint32_t index : mesh.indices) hash.le(index, 4);
    return hash.value();
}
// What must be true of a closed shape made by code: it checks; every triangle has an area and its front is the
// side its corners' normals are on; a normal has length 1 and a tangent lies across it; it is closed (each edge
// of a position pair is walked once each way).
void sound(const geom::Mesh& mesh, std::string_view name, bool closed) {
    expect(geom::check(mesh).has_value()) << name;
    expect(mesh.normals.size() == mesh.positions.size() && mesh.tangents.size() == mesh.positions.size() && mesh.uvs.size() == mesh.positions.size()) << name;
    std::size_t wrong = 0, flat = 0;
    for (std::size_t at = 0; at < mesh.indices.size(); at += 3) {
        const std::uint32_t one = mesh.indices[at], two = mesh.indices[at + 1], three = mesh.indices[at + 2];
        const Vec3 face = math::cross(mesh.positions[two] - mesh.positions[one], mesh.positions[three] - mesh.positions[one]);
        flat += math::length(face) < 1e-9f;
        for (const std::uint32_t corner : {one, two, three}) wrong += math::dot(face, mesh.normals[corner]) <= 0;
    }
    expect(eq(wrong, std::size_t{0})) << name << ": a triangle's front is where its normals point";
    expect(eq(flat, std::size_t{0})) << name << ": no triangle without an area";
    for (std::size_t index = 0; index < mesh.positions.size(); ++index) {
        const auto tangent = mesh.tangents[index];
        if (!near_to(math::length(mesh.normals[index]), 1.0f) || !near_to(math::dot(mesh.normals[index], tangent.xyz()), 0.0f) || !near_to(math::length(tangent.xyz()), 1.0f)) {
            expect(false) << name << std::format(": the frame at position {}", index);
            break;
        }
    }
    if (!closed) return;
    // By place, not by index: a seam's two columns are one edge.
    std::map<std::array<float, 6>, int> edges;
    const auto key = [&](std::uint32_t from, std::uint32_t to) { return std::array<float, 6>{mesh.positions[from].x, mesh.positions[from].y, mesh.positions[from].z, mesh.positions[to].x, mesh.positions[to].y, mesh.positions[to].z}; };
    for (std::size_t at = 0; at < mesh.indices.size(); at += 3)
        for (int side = 0; side < 3; ++side) ++edges[key(mesh.indices[at + side], mesh.indices[at + (side + 1) % 3])];
    std::size_t open = 0;
    for (const auto& [edge, times] : edges) {
        const std::array<float, 6> back{edge[3], edge[4], edge[5], edge[0], edge[1], edge[2]};
        const auto other = edges.find(back);
        open += other == edges.end() || other->second != times;
    }
    expect(eq(open, std::size_t{0})) << name << ": closed — each edge walked once each way";
}
}  // namespace

UTest cases{"geom.solid", [] {
    "a box: empty as made, grown by points, moved by a matrix to the box of its corners"_test = [] {
        geom::Box3 box;
        expect(box.empty() && !box.holds({0, 0, 0}));
        box.grow({1, 2, 3}), box.grow({-1, 0, 5});
        expect(box.low == Vec3{-1, 0, 3} && box.high == Vec3{1, 2, 5} && box.holds({0, 1, 4}) && !box.holds({0, 3, 4}));
        const geom::Box3 unit{{-1, -1, -1}, {1, 1, 1}};
        const auto turned = geom::moved(math::translation({10, 0, 0}) * math::rotation(math::Quat::about({0, 0, 1}, 0.78539816f)), unit);
        expect(near_to(turned.low.x, 10 - 1.41421356f) && near_to(turned.high.y, 1.41421356f) && near_to(turned.high.z, 1.0f)) << "a box turned an eighth is wider by √2";
        expect(geom::moved(math::Mat4{}, geom::Box3{}).empty());
        const std::array<Vec3, 4> points{{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {0, 0, 2}}};
        const geom::Sphere ball = geom::sphere_of(points);
        for (const Vec3 point : points) expect(math::length(point - ball.centre) <= ball.radius + 1e-6f);
    };

    "a ray: where it meets a box, a sphere, a triangle — and never behind where it starts"_test = [] {
        const geom::Box3 unit{{-1, -1, -1}, {1, 1, 1}};
        const geom::Ray ahead{{0, 0, 5}, {0, 0, -1}};
        expect(geom::meet(ahead, unit) == 4.0f);
        expect(!geom::meet({{0, 3, 5}, {0, 0, -1}}, unit).has_value() && !geom::meet({{0, 0, 5}, {0, 0, 1}}, unit).has_value());
        expect(geom::meet({{0, 0, 0}, {1, 0, 0}}, unit) == 0.0f) << "from inside: at once";
        expect(near_to(*geom::meet(ahead, geom::Sphere{{0, 0, 0}, 2}), 3.0f) && !geom::meet(ahead, geom::Sphere{{5, 0, 0}, 2}).has_value());
        expect(!geom::meet({{0, 0, 5}, {0, 0, 1}}, geom::Sphere{{0, 0, 0}, 2}).has_value());
        expect(near_to(*geom::meet(ahead, {-1, -1, 0}, {1, -1, 0}, {0, 1, 0}), 5.0f)) << "its front";
        expect(near_to(*geom::meet(ahead, {1, -1, 0}, {-1, -1, 0}, {0, 1, 0}), 5.0f)) << "its back as well";
        expect(!geom::meet(ahead, {2, 2, 0}, {3, 2, 0}, {2, 3, 0}).has_value() && !geom::meet({{0, 0, 5}, {0, 0, 1}}, {-1, -1, 0}, {1, -1, 0}, {0, 1, 0}).has_value());
    };

    "a frustum: what the view sees is kept, what is behind, beside or beyond is not; reversed with no far plane keeps what is far"_test = [] {
        const math::Mat4 view = math::look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
        const geom::Frustum seen = geom::Frustum::of(math::perspective(1.0f, 1.5f, 0.1f, 50.0f) * view);
        expect(eq(seen.count, 6u));
        const auto at = [](Vec3 centre) { return geom::Box3{centre - Vec3{0.5f, 0.5f, 0.5f}, centre + Vec3{0.5f, 0.5f, 0.5f}}; };
        expect(seen.holds(at({0, 0, 0})) && seen.holds(geom::Sphere{{0, 0, 0}, 1}));
        expect(!seen.holds(at({0, 0, 8})) && !seen.holds(geom::Sphere{{0, 0, 8}, 1})) << "behind the camera";
        expect(!seen.holds(at({30, 0, 0})) && !seen.holds(at({0, -30, 0})) && !seen.holds(at({0, 0, -60}))) << "beside, below, beyond the far plane";
        expect(seen.holds(at({0, 0, -44.8f}))) << "across the far plane: kept";
        const geom::Frustum far = geom::Frustum::of(math::perspective_reversed(1.0f, 1.5f, 0.1f) * view);
        expect(eq(far.count, 5u)) << "no far plane, and none invented";
        expect(far.holds(at({0, 0, -1e5f})) && !far.holds(at({0, 0, 8})) && !far.holds(at({30, 0, 0})));
        const geom::Frustum flat = geom::Frustum::of(math::orthographic(-2, 2, -1, 1, 0, 10) * view);
        expect(flat.holds(at({1.9f, 0, 0})) && !flat.holds(at({3, 0, 0})) && !flat.holds(at({0, 0, -6})));
        // Every point a view draws inside the frame is in its frustum: clip space and the planes agree.
        const math::Mat4 clip = math::perspective(1.0f, 1.5f, 0.1f, 50.0f) * view;
        std::size_t disagree = 0;
        for (int x = -6; x <= 6; ++x)
            for (int y = -6; y <= 6; ++y)
                for (int z = -12; z <= 6; ++z) {
                    const Vec3 point{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) * 4};
                    const math::Vec4 out = clip * math::Vec4{point, 1};
                    const bool inside = out.w > 0 && std::abs(out.x) <= out.w && std::abs(out.y) <= out.w && out.z >= 0 && out.z <= out.w;
                    disagree += inside && !seen.holds(geom::Sphere{point, 0});
                }
        expect(eq(disagree, std::size_t{0}));
    };

    "shapes made by code are sound: fronts where the normals point, frames of length 1, closed; the same bits everywhere"_test = [] {
        const geom::Mesh box = geom::box({1, 2, 3}), sheet = geom::sheet(4, 2, 4, 2), ball = geom::sphere(1.5f, 8, 12), can = geom::cylinder(1, 3, 10);
        sound(box, "box", true), sound(sheet, "sheet", false), sound(ball, "sphere", true), sound(can, "cylinder", true);
        expect(eq(box.triangles(), std::size_t{12}) && eq(sheet.triangles(), std::size_t{16}) && eq(ball.triangles(), std::size_t{2 * 12 * (8 - 1)}) && eq(can.triangles(), std::size_t{4 * 10}));
        expect(geom::bounds(box).low == Vec3{-1, -2, -3} && geom::bounds(box).high == Vec3{1, 2, 3});
        for (const Vec3 position : ball.positions) expect(near_to(math::length(position), 1.5f));
        // Normals from the triangles agree with the sphere's own, to what its facets allow.
        geom::Mesh recomputed = ball;
        geom::compute_normals(recomputed);
        float least = 1;
        for (std::size_t index = 0; index < ball.normals.size(); ++index)
            if (!(recomputed.normals[index] == Vec3{})) least = std::min(least, math::dot(ball.normals[index], recomputed.normals[index]));  // a pole's first column is in no triangle
        expect(least > 0.9f) << std::format("the worst agreement {}", least);
        std::println("    meshes {:016x} {:016x} {:016x} {:016x}", bits(box), bits(sheet), bits(ball), bits(can));
        expect(eq(bits(box), std::uint64_t{0x7cb5093f3013faa5}));
        expect(eq(bits(sheet), std::uint64_t{0x02a8302c3c3f5dd3}));
        expect(eq(bits(ball), std::uint64_t{0xcf8fe8a9921a5b48}));
        expect(eq(bits(can), std::uint64_t{0x387d388bf3ec6a0c}));
    };

    "a mesh that is wrong is refused, and says what: attributes not one a position, an index beyond, not a number"_test = [] {
        geom::Mesh mesh = geom::box({1, 1, 1});
        mesh.normals.pop_back();
        expect(geom::check(mesh).error() == "23 normals for 24 positions (one each, or none)");
        mesh = geom::box({1, 1, 1}), mesh.indices.push_back(0);
        expect(geom::check(mesh).error() == "37 indices: three a triangle");
        mesh = geom::box({1, 1, 1}), mesh.indices[5] = 99;
        expect(geom::check(mesh).error() == "index 5 names position 99, of 24");
        mesh = geom::box({1, 1, 1}), mesh.positions[3].y = std::numeric_limits<float>::quiet_NaN();
        expect(geom::check(mesh).error() == "position 3 is not a number");
        expect(!geom::clustered(mesh).has_value() && !geom::clustered(geom::box({1, 1, 1}), 0).has_value());
    };

    "stored in clusters: every triangle in one cluster, each within its bounds; where a cluster is said to face away, no triangle of it shows its front"_test = [] {
        const auto stored = geom::clustered(geom::sphere(2, 24, 48), 64);
        expect(fatal(stored.has_value()));
        expect(eq(geom::ClusteredMesh::version, 1u));
        std::size_t triangles = 0, next = 0, cones = 0;
        for (const geom::Cluster& cluster : stored->clusters) {
            expect(eq(std::size_t{cluster.first_index}, next) && cluster.triangle_count >= 1u && cluster.triangle_count <= 64u);
            next += std::size_t{cluster.triangle_count} * 3, triangles += cluster.triangle_count;
            cones += cluster.cone_cutoff <= 1;
            expect(cluster.error == 0 && cluster.level == 0u && std::isinf(cluster.parent_error)) << "one level today; the hierarchy's places are there";
            for (std::uint32_t index = 0; index < cluster.triangle_count * 3; ++index)
                if (math::length(stored->mesh.positions[stored->mesh.indices[cluster.first_index + index]] - cluster.bounds.centre) > cluster.bounds.radius + 1e-5f) expect(false) << "a corner outside its cluster's bounds";
        }
        expect(eq(triangles, stored->mesh.triangles()) && eq(next, stored->mesh.indices.size()));
        expect(cones > stored->clusters.size() / 2) << std::format("{} of {} clusters have a cone", cones, stored->clusters.size());
        // Eyes all around: a cluster culled by its cone has no triangle facing the eye; and from each eye some are culled.
        std::size_t culled = 0, wrongly = 0;
        math::SplitMix64 random{7};
        for (int probe = 0; probe < 200; ++probe) {
            const Vec3 eye = math::normalized(Vec3{static_cast<float>(math::unit24(random.next())) - 0.5f, static_cast<float>(math::unit24(random.next())) - 0.5f, static_cast<float>(math::unit24(random.next())) - 0.5f}) * (3.0f + static_cast<float>(probe % 5));
            for (const geom::Cluster& cluster : stored->clusters) {
                if (!geom::faces_away(cluster, eye)) continue;
                ++culled;
                for (std::uint32_t triangle = 0; triangle < cluster.triangle_count; ++triangle) {
                    const std::size_t at = cluster.first_index + std::size_t{triangle} * 3;
                    const Vec3 one = stored->mesh.positions[stored->mesh.indices[at]], two = stored->mesh.positions[stored->mesh.indices[at + 1]], three = stored->mesh.positions[stored->mesh.indices[at + 2]];
                    wrongly += math::dot(math::cross(two - one, three - one), eye - one) > 0;
                }
            }
        }
        expect(culled > 200u) << std::format("{} cluster views culled", culled);
        expect(eq(wrongly, std::size_t{0})) << "a triangle that faces the eye in a cluster said to face away";
    };
}};
