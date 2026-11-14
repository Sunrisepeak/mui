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
        mesh = geom::box({1, 1, 1}), mesh.second_uvs.assign(3, {0, 0});
        expect(geom::check(mesh).error() == "3 second uvs for 24 positions (one each, or none)");
        mesh = geom::box({1, 1, 1}), mesh.colours.assign(25, {1, 1, 1, 1});
        expect(geom::check(mesh).error() == "25 colours for 24 positions (one each, or none)");
        mesh = geom::box({1, 1, 1}), mesh.indices.push_back(0);
        expect(geom::check(mesh).error() == "37 indices: three a triangle");
        mesh = geom::box({1, 1, 1}), mesh.indices[5] = 99;
        expect(geom::check(mesh).error() == "index 5 names position 99, of 24");
        mesh = geom::box({1, 1, 1}), mesh.positions[3].y = std::numeric_limits<float>::quiet_NaN();
        expect(geom::check(mesh).error() == "position 3 is not a number");
        expect(!geom::clustered(mesh).has_value() && !geom::clustered(geom::box({1, 1, 1}), 0).has_value());
    };

    "clusters of sizes given: the same clusters as cut in order where the sizes are the same; sizes that are not the mesh's triangles are refused"_test = [] {
        const geom::Mesh ball = geom::sphere(2, 12, 24);
        const auto in_order = geom::clustered(ball, 50);
        expect(fatal(in_order.has_value()));
        std::vector<std::uint32_t> runs;
        for (const geom::Cluster& cluster : in_order->clusters) runs.push_back(cluster.triangle_count);
        const auto by_runs = geom::clustered(ball, runs);
        expect(fatal(by_runs.has_value()));
        expect(geom::stored(*by_runs) == geom::stored(*in_order));
        // Uneven sizes: each cluster begins where the one before ended.
        const std::array<std::uint32_t, 3> uneven{7, 100, static_cast<std::uint32_t>(ball.triangles()) - 107};
        const auto cut = geom::clustered(ball, uneven);
        expect(fatal(cut.has_value()));
        expect(eq(cut->clusters.size(), std::size_t{3}) && eq(cut->clusters[1].first_index, 21u) && eq(cut->clusters[2].first_index, 321u) && eq(cut->clusters[1].triangle_count, 100u));
        expect(geom::clustered(ball, std::array<std::uint32_t, 2>{7, 100}).error() == std::format("the clusters hold 107 triangles; the mesh has {}", ball.triangles()));
        expect(geom::clustered(ball, std::array<std::uint32_t, 2>{0, static_cast<std::uint32_t>(ball.triangles())}).error() == "a cluster has a triangle at least");
        expect(geom::clustered(geom::Mesh{}, std::span<const std::uint32_t>{}).has_value());
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
    "a polygon cut into triangles: corners − 2 of them, counter-clockwise, their areas the polygon's — given either way round; what crosses itself is refused"_test = [] {
        using math::Vec2;
        const std::array<Vec2, 6> ell{{{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 3}, {0, 3}}};
        // A star of five points: every other corner turns the wrong way.
        std::vector<Vec2> star;
        for (int point = 0; point < 10; ++point) {
            const double angle = math::det::two_pi * point / 10, reach = point % 2 ? 0.4 : 1.0;
            star.push_back({static_cast<float>(reach * math::det::cos(angle)), static_cast<float>(reach * math::det::sin(angle))});
        }
        const auto area_of = [](std::span<const Vec2> polygon, std::span<const std::uint32_t> triangles) {
            double sum = 0;
            bool all_left = true;
            for (std::size_t at = 0; at < triangles.size(); at += 3) {
                const Vec2 a = polygon[triangles[at]], b = polygon[triangles[at + 1]], c = polygon[triangles[at + 2]];
                const double twice = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y) - (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
                all_left = all_left && twice > 0;
                sum += twice / 2;
            }
            return all_left ? sum : -1.0;
        };
        const auto of_ell = geom::triangulated(ell), of_star = geom::triangulated(star);
        expect(fatal(of_ell.has_value() && of_star.has_value()));
        expect(eq(of_ell->size(), std::size_t{4 * 3}) && eq(of_star->size(), std::size_t{8 * 3}));
        expect(near_to(static_cast<float>(area_of(ell, *of_ell)), 4.0f) && near_to(static_cast<float>(area_of(star, *of_star)), 5 * 0.4f * static_cast<float>(math::det::sin(math::det::two_pi / 10)), 1e-3f));
        std::vector<Vec2> clockwise(ell.rbegin(), ell.rend());
        const auto turned = geom::triangulated(clockwise);
        expect(turned.has_value() && near_to(static_cast<float>(area_of(clockwise, *turned)), 4.0f)) << "given the other way round: the same triangles' worth, still counter-clockwise";
        const std::array<Vec2, 4> bow{{{0, 0}, {1, 1}, {1, 0}, {0, 1}}};
        const std::array<Vec2, 4> on_a_line{{{0, 0}, {1, 0}, {2, 0}, {1, 1}}};
        expect(!geom::triangulated(bow).has_value() && geom::triangulated(on_a_line).error().contains("on the line"));
        expect(geom::triangulated(std::span(ell).first(2)).error().contains("three at least"));
    };
    "an outline pushed through a depth and a profile turned about the axis are sound shapes, and hold the space they should"_test = [] {
        using math::Vec2;
        const std::array<Vec2, 6> ell{{{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 3}, {0, 3}}};
        const geom::Mesh pushed = *geom::extruded(ell, 0.5f);
        sound(pushed, "an L pushed", true);
        expect(near_to(static_cast<float>(geom::volume(pushed)), 4 * 0.5f)) << geom::volume(pushed);
        expect(geom::bounds(pushed).low == Vec3{0, 0, -0.25f} && geom::bounds(pushed).high == Vec3{2, 3, 0.25f});
        std::vector<Vec2> clockwise(ell.rbegin(), ell.rend());
        const geom::Mesh other = *geom::extruded(clockwise, 0.5f);
        sound(other, "an L given clockwise", true);
        expect(near_to(static_cast<float>(geom::volume(other)), 2.0f));
        expect(!geom::extruded(ell, 0).has_value() && !geom::extruded(std::span(ell).first(2), 1).has_value());
        // Bevelled: sound, closed, and the space it holds worked out — a square of 2, a depth of 1, a bevel of ¼
        // is a slab of ½ between two cut pyramids.
        const std::array<Vec2, 4> square{{{0, 0}, {2, 0}, {2, 2}, {0, 2}}};
        const geom::Mesh cut_square = *geom::extruded(square, 1, 0.25f);
        sound(cut_square, "a bevelled square", true);
        const float cut_pyramid = 0.25f / 6 * (4 + 2.25f + 4 * 1.75f * 1.75f);
        expect(near_to(static_cast<float>(geom::volume(cut_square)), 4 * 0.5f + 2 * cut_pyramid)) << geom::volume(cut_square);
        expect(geom::bounds(cut_square).low == Vec3{0, 0, -0.5f} && geom::bounds(cut_square).high == Vec3{2, 2, 0.5f}) << "it reaches as far as the plain one";
        expect(eq(cut_square.triangles(), std::size_t{2 * 2 + 4 * 2 + 4 * 4}));
        const geom::Mesh cut_ell = *geom::extruded(ell, 0.5f, 0.1f), cut_other = *geom::extruded(clockwise, 0.5f, 0.1f);
        sound(cut_ell, "a bevelled L", true);
        sound(cut_other, "a bevelled L given clockwise", true);
        expect(near_to(static_cast<float>(geom::volume(cut_ell)), static_cast<float>(geom::volume(cut_other))));
        expect(lt(geom::volume(cut_ell), 2.0) && gt(geom::volume(cut_ell), 1.8)) << geom::volume(cut_ell);
        expect(eq(geom::extruded(ell, 0.5f, 0).value().positions.size(), pushed.positions.size())) << "no bevel: the plain shape";
        expect(geom::extruded(square, 1, 0.5f).error().contains("less than half the depth"));
        expect(geom::extruded(ell, 3, 0.6f).error().contains("room")) << "the L's arms are 1 wide";
        const std::array<Vec2, 3> spike{{{0, 0}, {4, 0.05f}, {0, 0.1f}}};
        expect(geom::extruded(spike, 1, 0.01f).error().contains("too sharp"));

        // A can by its profile: the volume of a prism of 48 sides, exactly as one is.
        const std::array<Vec2, 4> can{{{0, 1}, {1, 1}, {1, -1}, {0, -1}}};
        const geom::Mesh turned = *geom::revolved(can, 48);
        sound(turned, "a can turned", true);
        const double prism = 2.0 * 48 * 0.5 * math::det::sin(math::det::two_pi / 48);
        expect(lt(std::abs(geom::volume(turned) - prism), 1e-4)) << geom::volume(turned) << " against " << prism;
        // Its corners stay corners: the lid's normals are up, the wall's are out.
        std::size_t up = 0, out = 0, between = 0;
        for (const Vec3 normal : turned.normals) up += normal.y == 1.0f || normal.y == -1.0f, out += normal.y == 0.0f, between += normal.y != 0.0f && std::abs(normal.y) != 1.0f;
        expect(gt(up, std::size_t{0}) && gt(out, std::size_t{0}) && eq(between, std::size_t{0}));
        // A ball by its profile, 24 stretches of a half circle: smooth (its normals are its own places), and the
        // space of a ball to what its facets allow.
        std::vector<Vec2> half_circle;
        for (int step = 0; step <= 24; ++step) {
            const double down = math::det::pi * step / 24;
            half_circle.push_back({step == 0 || step == 24 ? 0.0f : static_cast<float>(math::det::sin(down)), step == 0 ? 1.0f : step == 24 ? -1.0f : static_cast<float>(math::det::cos(down))});
        }
        const geom::Mesh ball = *geom::revolved(half_circle, 48);
        sound(ball, "a ball turned", true);
        float least = 1;
        for (std::size_t index = 0; index < ball.positions.size(); ++index) least = std::min(least, math::dot(ball.normals[index], math::normalized(ball.positions[index])));
        const double of_a_ball = 4.0 / 3.0 * math::det::pi;
        std::println("    a ball turned from 24 stretches, 48 around: {:.4f} of a ball's {:.4f}; its normals within {:.4f} of its places", geom::volume(ball), of_a_ball, least);
        expect(gt(least, 0.99f) && lt(std::abs(geom::volume(ball) - of_a_ball), of_a_ball * 0.01));
        // A vase: open at the top (it begins off the axis), so not closed — and sound for all that.
        const std::array<Vec2, 5> vase{{{0.5f, 2}, {0.3f, 1.2f}, {0.8f, 0.5f}, {0.6f, 0}, {0, 0}}};
        sound(*geom::revolved(vase, 24), "a vase", false);
        expect(geom::revolved(std::span(vase).first(1), 24).error().contains("two at least") && geom::revolved(vase, 2).error().contains("three at least"));
        const std::array<Vec2, 3> inside_out{{{0, 1}, {-1, 0}, {0, -1}}}, on_the_axis{{{0, 1}, {0, 0}, {1, 0}}};
        expect(geom::revolved(inside_out, 8).error().contains("never below 0") && geom::revolved(on_the_axis, 8).error().contains("both on the axis"));
        std::println("    made by code {:016x} {:016x} {:016x}", bits(pushed), bits(turned), bits(ball));
        expect(eq(bits(pushed), std::uint64_t{0x24d72b1884f47389}) && eq(bits(turned), std::uint64_t{0xe94946575782d6eb}) && eq(bits(ball), std::uint64_t{0xa7cbcb092680a157}));
    };
}};
