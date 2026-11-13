// mlibs.math.api:vector — the small vector types every simulation and renderer names, one
// definition each (single ownership; the foundation plan's §6.13: a solver family, the particles
// and what couples them take a position as the same type, so a third dimension adds a type here and
// nothing is defined a second time elsewhere).
//
// Plain values: components as written, component-wise arithmetic, no hidden normalisation. Linear
// algebra (matrices, quaternions, the three-dimensional types) joins this partition with the 3D
// work (M6.3).
export module mlibs.math.api:vector;

import std;

export namespace mlibs::math {

// Two components, single precision: a position, a velocity, an impulse, a direction in the plane.
struct Vec2 {
    float x = 0, y = 0;
    // Written out: a defaulted comparison here, used in a constant expression by an importer, ends
    // gcc 16.1.
    friend constexpr bool operator==(const Vec2& one, const Vec2& other) noexcept {
        return one.x == other.x && one.y == other.y;
    }
    friend constexpr Vec2 operator+(Vec2 one, Vec2 other) noexcept {
        return {one.x + other.x, one.y + other.y};
    }
    friend constexpr Vec2 operator-(Vec2 one, Vec2 other) noexcept {
        return {one.x - other.x, one.y - other.y};
    }
    friend constexpr Vec2 operator*(Vec2 vector, float scale) noexcept {
        return {vector.x * scale, vector.y * scale};
    }
    friend constexpr Vec2 operator-(Vec2 vector) noexcept {
        return {-vector.x, -vector.y};
    }
};
[[nodiscard]] constexpr float dot(Vec2 one, Vec2 other) noexcept {
    return one.x * other.x + one.y * other.y;
}
// The z of the cross product of the two in the plane: positive when `other` is counter-clockwise of
// `one`.
[[nodiscard]] constexpr float cross(Vec2 one, Vec2 other) noexcept {
    return one.x * other.y - one.y * other.x;
}

}  // namespace mlibs::math
