// mlibs.geom.api:solid — geometry in three dimensions: boxes, spheres, planes, rays and a view's frustum, and
// what meets what. The conventions are mlibs.math's (right-handed, +Y up, depth 0 … 1).
//
//     const geom::Frustum seen = geom::Frustum::of(projection * view);
//     if (seen.holds(box)) draw(mesh);                      // culling: never says no to what is seen
//     if (const auto hit = geom::meet(ray, box)) pick(*hit);  // picking
//
// Plain values. A query that culls is conservative: it may keep what is just outside, it never drops what is in.
export module mlibs.geom.api:solid;

import std;
import mlibs.math;

export namespace mlibs::geom {

using mlibs::math::Mat4;
using mlibs::math::Vec3;

// A box along the axes. As made: empty (it holds nothing, and growing it by a point makes it that point).
struct Box3 {
    Vec3 low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 high{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    [[nodiscard]] constexpr bool empty() const noexcept { return low.x > high.x || low.y > high.y || low.z > high.z; }
    [[nodiscard]] constexpr Vec3 centre() const noexcept { return (low + high) * 0.5f; }
    [[nodiscard]] constexpr Vec3 half() const noexcept { return (high - low) * 0.5f; }
    constexpr void grow(Vec3 point) noexcept {
        low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
        high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
    }
    constexpr void join(const Box3& other) noexcept {
        if (!other.empty()) grow(other.low), grow(other.high);
    }
    [[nodiscard]] constexpr bool holds(Vec3 point) const noexcept {
        return point.x >= low.x && point.x <= high.x && point.y >= low.y && point.y <= high.y && point.z >= low.z && point.z <= high.z;
    }
    [[nodiscard]] static constexpr Box3 of(std::span<const Vec3> points) noexcept {
        Box3 out;
        for (const Vec3 point : points) out.grow(point);
        return out;
    }
};
// The box that holds `box` after `matrix` moved it (an affine matrix: the box of its eight corners, found
// from the centre and the half extents).
[[nodiscard]] constexpr Box3 moved(const Mat4& matrix, const Box3& box) noexcept {
    if (box.empty()) return {};
    const Vec3 centre = mlibs::math::point(matrix, box.centre()), half = box.half();
    const auto reach = [&](std::size_t row) {
        const auto size = [](float value) { return value < 0 ? -value : value; };
        return size(matrix.at(0, row)) * half.x + size(matrix.at(1, row)) * half.y + size(matrix.at(2, row)) * half.z;
    };
    const Vec3 extent{reach(0), reach(1), reach(2)};
    return {centre - extent, centre + extent};
}

struct Sphere {
    Vec3 centre;
    float radius = 0;
};
// A sphere that holds the points: about the middle of their box, as far as the farthest. Not the smallest —
// within a few percent for what a mesh's cluster is — and the same bits everywhere.
[[nodiscard]] inline Sphere sphere_of(std::span<const Vec3> points) noexcept {
    const Box3 box = Box3::of(points);
    if (box.empty()) return {};
    Sphere out{box.centre(), 0};
    float farthest = 0;
    for (const Vec3 point : points) farthest = std::max(farthest, mlibs::math::dot(point - out.centre, point - out.centre));
    out.radius = std::sqrt(farthest);
    return out;
}

// The points where dot(normal, point) + distance is 0; in front of it that is above 0. `normal` of length 1.
struct Plane {
    Vec3 normal{0, 1, 0};
    float distance = 0;
    [[nodiscard]] constexpr float above(Vec3 point) const noexcept { return mlibs::math::dot(normal, point) + distance; }
};

struct Ray {
    Vec3 from;
    Vec3 along{0, 0, -1};  // of length 1 for a hit's distance to be a length
};
// Where a ray meets: how far along it (never behind where it starts).
[[nodiscard]] constexpr std::optional<float> meet(const Ray& ray, const Box3& box) noexcept {
    if (box.empty()) return std::nullopt;
    float nearest = 0, farthest = std::numeric_limits<float>::max();
    const auto slab = [&](float from, float along, float low, float high) {
        if (along == 0) return from >= low && from <= high;
        float first = (low - from) / along, second = (high - from) / along;
        if (first > second) std::swap(first, second);
        nearest = std::max(nearest, first), farthest = std::min(farthest, second);
        return nearest <= farthest;
    };
    if (!slab(ray.from.x, ray.along.x, box.low.x, box.high.x) || !slab(ray.from.y, ray.along.y, box.low.y, box.high.y) ||
        !slab(ray.from.z, ray.along.z, box.low.z, box.high.z))
        return std::nullopt;
    return nearest;
}
[[nodiscard]] inline std::optional<float> meet(const Ray& ray, const Sphere& sphere) noexcept {
    const Vec3 to = sphere.centre - ray.from;
    const float along = mlibs::math::dot(to, ray.along);
    const float apart = mlibs::math::dot(to, to) - along * along, reach = sphere.radius * sphere.radius;
    if (apart > reach) return std::nullopt;
    const float half = std::sqrt(reach - apart);
    if (along + half < 0) return std::nullopt;
    return std::max(0.0f, along - half);
}
// A triangle, counter-clockwise seen from its front; met from either side.
[[nodiscard]] constexpr std::optional<float> meet(const Ray& ray, Vec3 first, Vec3 second, Vec3 third) noexcept {
    const Vec3 one = second - first, other = third - first, across = mlibs::math::cross(ray.along, other);
    const float volume = mlibs::math::dot(one, across);
    if (volume > -1e-12f && volume < 1e-12f) return std::nullopt;
    const float inverse = 1.0f / volume;
    const Vec3 to = ray.from - first;
    const float u = mlibs::math::dot(to, across) * inverse;
    if (u < 0 || u > 1) return std::nullopt;
    const Vec3 turn = mlibs::math::cross(to, one);
    const float v = mlibs::math::dot(ray.along, turn) * inverse;
    if (v < 0 || u + v > 1) return std::nullopt;
    const float distance = mlibs::math::dot(other, turn) * inverse;
    if (distance < 0) return std::nullopt;
    return distance;
}

// What a view sees: the planes of its clip volume, each facing in. From a matrix that takes the world to clip
// space with depth 0 … 1 — a perspective, an orthographic box, and a reversed perspective with no far plane (which
// has five planes: the sixth is left out, not invented).
struct Frustum {
    std::array<Plane, 6> planes{};
    std::uint32_t count = 0;
    [[nodiscard]] static Frustum of(const Mat4& world_to_clip) noexcept {
        Frustum out;
        const auto row = [&](std::size_t index) { return std::array<float, 4>{world_to_clip.at(0, index), world_to_clip.at(1, index), world_to_clip.at(2, index), world_to_clip.at(3, index)}; };
        const auto x = row(0), y = row(1), z = row(2), w = row(3);
        const auto add = [&](float a, float b, float c, float d) {
            const float size = std::sqrt(a * a + b * b + c * c);
            if (!(size > 1e-20f)) return;  // no such plane (a far plane at infinity)
            out.planes[out.count++] = {{a / size, b / size, c / size}, d / size};
        };
        add(w[0] + x[0], w[1] + x[1], w[2] + x[2], w[3] + x[3]);  // −w ≤ x
        add(w[0] - x[0], w[1] - x[1], w[2] - x[2], w[3] - x[3]);  // x ≤ w
        add(w[0] + y[0], w[1] + y[1], w[2] + y[2], w[3] + y[3]);
        add(w[0] - y[0], w[1] - y[1], w[2] - y[2], w[3] - y[3]);
        add(z[0], z[1], z[2], z[3]);                              // 0 ≤ z
        add(w[0] - z[0], w[1] - z[1], w[2] - z[2], w[3] - z[3]);  // z ≤ w
        return out;
    }
    // False only where the whole of it is behind one plane.
    [[nodiscard]] constexpr bool holds(const Sphere& sphere) const noexcept {
        for (std::uint32_t index = 0; index < count; ++index)
            if (planes[index].above(sphere.centre) < -sphere.radius) return false;
        return true;
    }
    [[nodiscard]] constexpr bool holds(const Box3& box) const noexcept {
        if (box.empty()) return false;
        const Vec3 centre = box.centre(), half = box.half();
        for (std::uint32_t index = 0; index < count; ++index) {
            const Vec3 normal = planes[index].normal;
            const auto size = [](float value) { return value < 0 ? -value : value; };
            const float reach = size(normal.x) * half.x + size(normal.y) * half.y + size(normal.z) * half.z;
            if (planes[index].above(centre) < -reach) return false;
        }
        return true;
    }
};

}  // namespace mlibs::geom
