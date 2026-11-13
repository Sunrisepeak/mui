module mlibs.geom.api;

import std;
import mlibs.math;

namespace mlibs::geom {

namespace {

namespace math = mlibs::math;
constexpr double turn = 6.283185307179586;

double twice_area(std::span<const Vec2> polygon) noexcept {
    double sum = 0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Vec2 here = polygon[index], next = polygon[(index + 1) % polygon.size()];
        sum += static_cast<double>(here.x) * next.y - static_cast<double>(next.x) * here.y;
    }
    return sum;
}
// Above 0: `c` is to the left of the way from `a` to `b`.
double side(Vec2 a, Vec2 b, Vec2 c) noexcept {
    return (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y) - (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
}
bool finite(Vec2 value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y); }

}  // namespace

std::expected<std::vector<std::uint32_t>, std::string> triangulated(std::span<const Vec2> polygon) {
    const std::size_t count = polygon.size();
    if (count < 3) return std::unexpected(std::format("a polygon of {} corners: three at least", count));
    for (std::size_t index = 0; index < count; ++index)
        if (!finite(polygon[index])) return std::unexpected(std::format("corner {} is not a number", index));
    const double area = twice_area(polygon);
    if (area == 0) return std::unexpected(std::string("a polygon with no area"));
    // The corners still to be cut off, in counter-clockwise order.
    std::vector<std::uint32_t> left(count);
    for (std::uint32_t index = 0; index < count; ++index) left[index] = area > 0 ? index : static_cast<std::uint32_t>(count - 1 - index);
    for (std::size_t index = 0; index < count; ++index) {
        const Vec2 before = polygon[left[(index + count - 1) % count]], here = polygon[left[index]], after = polygon[left[(index + 1) % count]];
        if (side(before, here, after) == 0) return std::unexpected(std::format("corner {} is on the line of its neighbours: leave it out", left[index]));
    }
    std::vector<std::uint32_t> triangles;
    triangles.reserve((count - 2) * 3);
    // An ear: a corner that turns left, with no other corner inside its triangle. Cut it off; again.
    while (left.size() > 3) {
        bool cut = false;
        for (std::size_t index = 0; index < left.size(); ++index) {
            const std::uint32_t before = left[(index + left.size() - 1) % left.size()], here = left[index], after = left[(index + 1) % left.size()];
            const Vec2 a = polygon[before], b = polygon[here], c = polygon[after];
            if (side(a, b, c) <= 0) continue;
            bool holds_another = false;
            for (const std::uint32_t other : left) {
                if (other == before || other == here || other == after) continue;
                const Vec2 point = polygon[other];
                if (side(a, b, point) >= 0 && side(b, c, point) >= 0 && side(c, a, point) >= 0) {
                    holds_another = true;
                    break;
                }
            }
            if (holds_another) continue;
            triangles.insert(triangles.end(), {before, here, after});
            left.erase(left.begin() + static_cast<std::ptrdiff_t>(index));
            cut = true;
            break;
        }
        if (!cut) return std::unexpected(std::string("the polygon crosses itself, or touches itself: no corner of it can be cut off"));
    }
    if (side(polygon[left[0]], polygon[left[1]], polygon[left[2]]) <= 0) return std::unexpected(std::string("the polygon crosses itself: what is left of it faces the other way"));
    triangles.insert(triangles.end(), {left[0], left[1], left[2]});
    return triangles;
}

std::expected<Mesh, std::string> revolved(std::span<const Vec2> profile, std::uint32_t sectors) {
    if (profile.size() < 2) return std::unexpected(std::format("a profile of {} points: two at least", profile.size()));
    if (sectors < 3) return std::unexpected(std::format("{} sectors around: three at least", sectors));
    for (std::size_t index = 0; index < profile.size(); ++index) {
        if (!finite(profile[index])) return std::unexpected(std::format("point {} of the profile is not a number", index));
        if (profile[index].x < 0) return std::unexpected(std::format("point {} of the profile is at radius {}: never below 0", index, profile[index].x));
    }
    // A stretch's own normal in the profile's plane (radius, height): to the left of the way down it.
    const std::size_t stretches = profile.size() - 1;
    std::vector<Vec2> facing(stretches);
    std::vector<float> along(profile.size(), 0.0f);
    for (std::size_t index = 0; index < stretches; ++index) {
        const Vec2 from = profile[index], to = profile[index + 1];
        const float dr = to.x - from.x, dh = to.y - from.y, length = std::sqrt(dr * dr + dh * dh);
        if (!(length > 0)) return std::unexpected(std::format("points {} and {} of the profile are the same place", index, index + 1));
        if (from.x == 0 && to.x == 0) return std::unexpected(std::format("points {} and {} of the profile are both on the axis: a stretch with no surface", index, index + 1));
        facing[index] = {-dh / length, dr / length};
        along[index + 1] = along[index] + length;
    }
    // At a point between two stretches: one normal for both where they meet gently, each its own at a corner.
    const auto shared = [&](std::size_t one, std::size_t other) -> std::optional<Vec2> {
        const Vec2 a = facing[one], b = facing[other];
        if (a.x * b.x + a.y * b.y < 0.5f) return std::nullopt;
        const float x = a.x + b.x, y = a.y + b.y, length = std::sqrt(x * x + y * y);
        return Vec2{x / length, y / length};
    };
    Mesh mesh;
    const std::uint32_t stride = sectors + 1;
    const auto ring = [&](Vec2 at, Vec2 normal, float v) {
        for (std::uint32_t sector = 0; sector <= sectors; ++sector) {
            const double around = turn * (sector % sectors) / sectors;
            const auto cosine = static_cast<float>(math::det::cos(around)), sine = static_cast<float>(math::det::sin(around));
            mesh.positions.push_back({at.x * cosine, at.y, -at.x * sine});
            mesh.normals.push_back({normal.x * cosine, normal.y, -normal.x * sine});
            mesh.tangents.push_back({-sine, 0, -cosine, 1});
            mesh.uvs.push_back({static_cast<float>(sector) / static_cast<float>(sectors), v});
        }
    };
    const float whole = along.back();
    for (std::size_t index = 0; index < stretches; ++index) {
        const Vec2 above = index > 0 ? shared(index - 1, index).value_or(facing[index]) : facing[index];
        const Vec2 below = index + 1 < stretches ? shared(index, index + 1).value_or(facing[index]) : facing[index];
        const auto first = static_cast<std::uint32_t>(mesh.positions.size());
        ring(profile[index], above, along[index] / whole);
        ring(profile[index + 1], below, along[index + 1] / whole);
        for (std::uint32_t sector = 0; sector < sectors; ++sector) {
            const std::uint32_t corner = first + sector;
            // On the axis one of a band's two triangles has no area: left out.
            if (profile[index].x != 0) for (const std::uint32_t at : {corner, corner + stride, corner + 1}) mesh.indices.push_back(at);
            if (profile[index + 1].x != 0) for (const std::uint32_t at : {corner + 1, corner + stride, corner + stride + 1}) mesh.indices.push_back(at);
        }
    }
    return mesh;
}

std::expected<Mesh, std::string> extruded(std::span<const Vec2> given, float depth, float bevel) {
    if (!(depth > 0) || !std::isfinite(depth)) return std::unexpected(std::format("a depth of {}: above 0", depth));
    if (!(bevel >= 0) || !std::isfinite(bevel) || !(bevel * 2 < depth)) return std::unexpected(std::format("a bevel of {} in a depth of {}: 0 or more, and less than half the depth", bevel, depth));
    if (auto whole = triangulated(given); !whole) return std::unexpected(whole.error());
    const std::size_t count = given.size();
    const bool turned = twice_area(given) < 0;  // given clockwise: walked the other way
    // The faces' own outline: the given one drawn in by the bevel, each corner along the line between its two edges.
    std::vector<Vec2> drawn_in(given.begin(), given.end());
    if (bevel > 0) {
        const float side = turned ? -1.0f : 1.0f;
        for (std::size_t index = 0; index < count; ++index) {
            const Vec2 before = given[(index + count - 1) % count], here = given[index], after = given[(index + 1) % count];
            const float ax = here.x - before.x, ay = here.y - before.y, bx = after.x - here.x, by = after.y - here.y;
            const float a_long = std::sqrt(ax * ax + ay * ay), b_long = std::sqrt(bx * bx + by * by);
            const Vec2 in_a{-ay / a_long * side, ax / a_long * side}, in_b{-by / b_long * side, bx / b_long * side};
            const float both = 1 + in_a.x * in_b.x + in_a.y * in_b.y;
            if (!(both > 0.05f)) return std::unexpected(std::format("corner {} is too sharp to bevel", index));
            drawn_in[index] = {here.x + (in_a.x + in_b.x) * bevel / both, here.y + (in_a.y + in_b.y) * bevel / both};
        }
        if ((twice_area(drawn_in) < 0) != turned) return std::unexpected(std::format("a bevel of {} is more than the outline has room for", bevel));
    }
    const std::span<const Vec2> outline(drawn_in);
    auto cut = triangulated(outline);
    if (!cut) return std::unexpected(std::format("a bevel of {} is more than the outline has room for ({})", bevel, cut.error()));
    const auto corner = [&](std::size_t index) { return given[turned ? count - 1 - index : index]; };
    const auto rim = [&](std::size_t index) { return drawn_in[turned ? count - 1 - index : index]; };
    Vec2 low = outline[0], high = outline[0];
    for (const Vec2 point : outline) low = {std::min(low.x, point.x), std::min(low.y, point.y)}, high = {std::max(high.x, point.x), std::max(high.y, point.y)};
    const float half = depth * 0.5f, wall = half - bevel;  // the wall stops a bevel short of each face
    Mesh mesh;
    // The two faces: the outline's own triangles in front (+Z), the same turned over behind.
    for (const float z : {half, -half}) {
        const auto first = static_cast<std::uint32_t>(mesh.positions.size());
        for (const Vec2 point : outline) {
            mesh.positions.push_back({point.x, point.y, z});
            mesh.normals.push_back({0, 0, z > 0 ? 1.0f : -1.0f});
            mesh.tangents.push_back({1, 0, 0, z > 0 ? 1.0f : -1.0f});
            mesh.uvs.push_back({(point.x - low.x) / std::max(high.x - low.x, 1e-20f), 1 - (point.y - low.y) / std::max(high.y - low.y, 1e-20f)});
        }
        for (std::size_t at = 0; at < cut->size(); at += 3) {
            if (z > 0) mesh.indices.insert(mesh.indices.end(), {first + (*cut)[at], first + (*cut)[at + 1], first + (*cut)[at + 2]});
            else mesh.indices.insert(mesh.indices.end(), {first + (*cut)[at], first + (*cut)[at + 2], first + (*cut)[at + 1]});
        }
    }
    // The wall: a flat piece an edge, facing out (to the right of the way round).
    float round = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const Vec2 from = corner(index), to = corner((index + 1) % count);
        round += std::sqrt((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y));
    }
    float walked = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const Vec2 from = corner(index), to = corner((index + 1) % count);
        const float dx = to.x - from.x, dy = to.y - from.y, length = std::sqrt(dx * dx + dy * dy);
        const Vec3 normal{dy / length, -dx / length, 0};
        const auto first = static_cast<std::uint32_t>(mesh.positions.size());
        const float u0 = walked / round, u1 = (walked + length) / round;
        walked += length;
        const std::array<std::pair<Vec3, Vec2>, 4> corners{{{{from.x, from.y, -wall}, {u0, 1}}, {{to.x, to.y, -wall}, {u1, 1}}, {{to.x, to.y, wall}, {u1, 0}}, {{from.x, from.y, wall}, {u0, 0}}}};
        for (const auto& [place, uv] : corners) {
            mesh.positions.push_back(place);
            mesh.normals.push_back(normal);
            mesh.tangents.push_back({dx / length, dy / length, 0, 1});
            mesh.uvs.push_back(uv);
        }
        for (const std::uint32_t at : {first, first + 1, first + 2, first, first + 2, first + 3}) mesh.indices.push_back(at);
        if (!(bevel > 0)) continue;
        // The bevel: a flat piece from the wall's rim to the face's, front and back, facing half out and half along Z.
        const Vec2 rim_from = rim(index), rim_to = rim((index + 1) % count);
        for (const float way : {1.0f, -1.0f}) {
            const auto begun = static_cast<std::uint32_t>(mesh.positions.size());
            const Vec3 slanted = math::normalized(Vec3{normal.x, normal.y, way});
            const std::array<Vec3, 4> piece = way > 0 ? std::array<Vec3, 4>{Vec3{from.x, from.y, wall}, Vec3{to.x, to.y, wall}, Vec3{rim_to.x, rim_to.y, half}, Vec3{rim_from.x, rim_from.y, half}}
                                                      : std::array<Vec3, 4>{Vec3{rim_from.x, rim_from.y, -half}, Vec3{rim_to.x, rim_to.y, -half}, Vec3{to.x, to.y, -wall}, Vec3{from.x, from.y, -wall}};
            const std::array<Vec2, 4> placed{Vec2{u0, way > 0 ? 0.0f : 1.0f}, Vec2{u1, way > 0 ? 0.0f : 1.0f}, Vec2{u1, way > 0 ? 0.0f : 1.0f}, Vec2{u0, way > 0 ? 0.0f : 1.0f}};
            for (std::size_t which = 0; which < 4; ++which) {
                mesh.positions.push_back(piece[which]);
                mesh.normals.push_back(slanted);
                mesh.tangents.push_back({dx / length, dy / length, 0, 1});
                mesh.uvs.push_back(placed[which]);
            }
            for (const std::uint32_t at : {begun, begun + 1, begun + 2, begun, begun + 2, begun + 3}) mesh.indices.push_back(at);
        }
    }
    return mesh;
}

double volume(const Mesh& mesh) noexcept {
    double six = 0;
    for (std::size_t at = 0; at + 2 < mesh.indices.size(); at += 3) {
        const Vec3 a = mesh.positions[mesh.indices[at]], b = mesh.positions[mesh.indices[at + 1]], c = mesh.positions[mesh.indices[at + 2]];
        six += static_cast<double>(a.x) * (static_cast<double>(b.y) * c.z - static_cast<double>(b.z) * c.y) + static_cast<double>(a.y) * (static_cast<double>(b.z) * c.x - static_cast<double>(b.x) * c.z) +
               static_cast<double>(a.z) * (static_cast<double>(b.x) * c.y - static_cast<double>(b.y) * c.x);
    }
    return six / 6;
}

}  // namespace mlibs::geom
