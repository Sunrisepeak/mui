// mlibs.math.api:linear — linear algebra in three dimensions: Vec3, Vec4, Quat, Mat4, Transform. One
// definition each (single ownership): what a renderer, a solver and a scene all name.
//
//     using namespace mlibs::math;
//     const Mat4 model = Transform{.translation = {0, 1, 0}, .rotation = Quat::about({0, 1, 0}, 0.5f)}.matrix();
//     const Mat4 view = look_at({0, 2, 5}, {0, 1, 0}, {0, 1, 0});
//     const Mat4 clip = perspective(1.0f, 16.0f / 9.0f, 0.1f, 100.0f) * view * model;
//
// Conventions, said once and the same everywhere:
//   - right-handed, +Y up, a camera looks along −Z (glTF's);
//   - a matrix is stored a column after a column and multiplies a column vector: `a * b` is b, then a;
//   - clip space has depth 0 … 1 (WebGPU, Vulkan, Metal, Direct3D), y up;
//   - angles are radians; a quaternion is (x, y, z, w), the identity (0, 0, 0, 1).
// Plain values with no hidden normalisation. What is built from an angle uses det::sin and det::cos,
// and the package forbids contraction and fast-math, so a matrix is the same bits on every platform.
export module mlibs.math.api:linear;

import std;
import :det;
import :vector;

export namespace mlibs::math {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    friend constexpr bool operator==(const Vec3& one, const Vec3& other) noexcept {
        return one.x == other.x && one.y == other.y && one.z == other.z;
    }
    friend constexpr Vec3 operator+(Vec3 one, Vec3 other) noexcept {
        return {one.x + other.x, one.y + other.y, one.z + other.z};
    }
    friend constexpr Vec3 operator-(Vec3 one, Vec3 other) noexcept {
        return {one.x - other.x, one.y - other.y, one.z - other.z};
    }
    friend constexpr Vec3 operator*(Vec3 vector, float scale) noexcept {
        return {vector.x * scale, vector.y * scale, vector.z * scale};
    }
    friend constexpr Vec3 operator-(Vec3 vector) noexcept { return {-vector.x, -vector.y, -vector.z}; }
};
[[nodiscard]] constexpr float dot(Vec3 one, Vec3 other) noexcept {
    return one.x * other.x + one.y * other.y + one.z * other.z;
}
[[nodiscard]] constexpr Vec3 cross(Vec3 one, Vec3 other) noexcept {
    return {one.y * other.z - one.z * other.y, one.z * other.x - one.x * other.z,
            one.x * other.y - one.y * other.x};
}
// Component by component.
[[nodiscard]] constexpr Vec3 scaled(Vec3 vector, Vec3 by) noexcept {
    return {vector.x * by.x, vector.y * by.y, vector.z * by.z};
}
[[nodiscard]] inline float length(Vec3 vector) noexcept { return std::sqrt(dot(vector, vector)); }
// Of length 1; the zero vector stays zero (no direction is invented).
[[nodiscard]] inline Vec3 normalized(Vec3 vector) noexcept {
    const float size = length(vector);
    return size > 0 ? vector * (1.0f / size) : Vec3{};
}
[[nodiscard]] constexpr Vec3 mix(Vec3 from, Vec3 to, float share) noexcept {
    return from + (to - from) * share;
}

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr Vec4() noexcept = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) noexcept : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(Vec3 xyz_, float w_) noexcept : x(xyz_.x), y(xyz_.y), z(xyz_.z), w(w_) {}
    [[nodiscard]] constexpr Vec3 xyz() const noexcept { return {x, y, z}; }
    friend constexpr bool operator==(const Vec4& one, const Vec4& other) noexcept {
        return one.x == other.x && one.y == other.y && one.z == other.z && one.w == other.w;
    }
};

// A rotation. Meant to be of length 1: `about` and `between` make it so, a product of two keeps it
// so to rounding, and `normalized` restores it after many.
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    friend constexpr bool operator==(const Quat& one, const Quat& other) noexcept {
        return one.x == other.x && one.y == other.y && one.z == other.z && one.w == other.w;
    }
    // `other`, then `one`.
    friend constexpr Quat operator*(const Quat& one, const Quat& other) noexcept {
        return {one.w * other.x + one.x * other.w + one.y * other.z - one.z * other.y,
                one.w * other.y - one.x * other.z + one.y * other.w + one.z * other.x,
                one.w * other.z + one.x * other.y - one.y * other.x + one.z * other.w,
                one.w * other.w - one.x * other.x - one.y * other.y - one.z * other.z};
    }
    // By `radians` about `axis` (made of length 1 here; a zero axis: no rotation), counter-clockwise
    // seen from the axis' tip.
    [[nodiscard]] static Quat about(Vec3 axis, float radians) noexcept {
        const Vec3 unit = normalized(axis);
        if (unit == Vec3{}) return {};
        const auto half = static_cast<double>(radians) * 0.5;
        const auto sine = static_cast<float>(det::sin(half));
        return {unit.x * sine, unit.y * sine, unit.z * sine, static_cast<float>(det::cos(half))};
    }
};
[[nodiscard]] constexpr Quat conjugate(const Quat& rotation) noexcept {
    return {-rotation.x, -rotation.y, -rotation.z, rotation.w};
}
[[nodiscard]] constexpr float dot(const Quat& one, const Quat& other) noexcept {
    return one.x * other.x + one.y * other.y + one.z * other.z + one.w * other.w;
}
// Of length 1; what has no length is the identity.
[[nodiscard]] inline Quat normalized(const Quat& rotation) noexcept {
    const float size = std::sqrt(dot(rotation, rotation));
    if (!(size > 0)) return {};
    const float inverse = 1.0f / size;
    return {rotation.x * inverse, rotation.y * inverse, rotation.z * inverse, rotation.w * inverse};
}
// `vector` turned by `rotation` (of length 1).
[[nodiscard]] constexpr Vec3 rotate(const Quat& rotation, Vec3 vector) noexcept {
    const Vec3 axis{rotation.x, rotation.y, rotation.z};
    const Vec3 twice = cross(axis, vector) * 2.0f;
    return vector + twice * rotation.w + cross(axis, twice);
}
// The way from one rotation to another along the shorter arc, of length 1: a normalised mix — the
// same path as the spherical one, at a speed that is not quite even, and no trigonometry.
[[nodiscard]] inline Quat mix(const Quat& from, const Quat& to, float share) noexcept {
    const float side = dot(from, to) < 0 ? -1.0f : 1.0f;
    return normalized({from.x + (to.x * side - from.x) * share, from.y + (to.y * side - from.y) * share,
                       from.z + (to.z * side - from.z) * share, from.w + (to.w * side - from.w) * share});
}

// Four columns of four: `at(column, row)`. As made: the identity.
struct Mat4 {
    std::array<float, 16> values{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    [[nodiscard]] constexpr float at(std::size_t column, std::size_t row) const noexcept {
        return values[column * 4 + row];
    }
    constexpr float& at(std::size_t column, std::size_t row) noexcept { return values[column * 4 + row]; }
    friend constexpr bool operator==(const Mat4& one, const Mat4& other) noexcept {
        for (std::size_t index = 0; index < 16; ++index)
            if (one.values[index] != other.values[index]) return false;
        return true;
    }
    // `other`, then `one`.
    friend constexpr Mat4 operator*(const Mat4& one, const Mat4& other) noexcept {
        Mat4 out;
        for (std::size_t column = 0; column < 4; ++column)
            for (std::size_t row = 0; row < 4; ++row)
                out.at(column, row) = one.at(0, row) * other.at(column, 0) + one.at(1, row) * other.at(column, 1) +
                                      one.at(2, row) * other.at(column, 2) + one.at(3, row) * other.at(column, 3);
        return out;
    }
    friend constexpr Vec4 operator*(const Mat4& matrix, Vec4 vector) noexcept {
        const auto row = [&](std::size_t index) {
            return matrix.at(0, index) * vector.x + matrix.at(1, index) * vector.y +
                   matrix.at(2, index) * vector.z + matrix.at(3, index) * vector.w;
        };
        return {row(0), row(1), row(2), row(3)};
    }
};
[[nodiscard]] constexpr Mat4 transposed(const Mat4& matrix) noexcept {
    Mat4 out;
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row) out.at(column, row) = matrix.at(row, column);
    return out;
}
[[nodiscard]] constexpr Mat4 translation(Vec3 by) noexcept {
    Mat4 out;
    out.at(3, 0) = by.x, out.at(3, 1) = by.y, out.at(3, 2) = by.z;
    return out;
}
[[nodiscard]] constexpr Mat4 scaling(Vec3 by) noexcept {
    Mat4 out;
    out.at(0, 0) = by.x, out.at(1, 1) = by.y, out.at(2, 2) = by.z;
    return out;
}
// The matrix of a rotation of length 1.
[[nodiscard]] constexpr Mat4 rotation(const Quat& turn) noexcept {
    const float xx = turn.x * turn.x, yy = turn.y * turn.y, zz = turn.z * turn.z;
    const float xy = turn.x * turn.y, xz = turn.x * turn.z, yz = turn.y * turn.z;
    const float wx = turn.w * turn.x, wy = turn.w * turn.y, wz = turn.w * turn.z;
    Mat4 out;
    out.at(0, 0) = 1 - 2 * (yy + zz), out.at(0, 1) = 2 * (xy + wz), out.at(0, 2) = 2 * (xz - wy);
    out.at(1, 0) = 2 * (xy - wz), out.at(1, 1) = 1 - 2 * (xx + zz), out.at(1, 2) = 2 * (yz + wx);
    out.at(2, 0) = 2 * (xz + wy), out.at(2, 1) = 2 * (yz - wx), out.at(2, 2) = 1 - 2 * (xx + yy);
    return out;
}
// A point moved by the matrix (its w taken as 1 and the result's w divided out where it is not 1:
// through a projection too).
[[nodiscard]] constexpr Vec3 point(const Mat4& matrix, Vec3 at) noexcept {
    const Vec4 moved = matrix * Vec4{at, 1.0f};
    if (moved.w == 1.0f || moved.w == 0.0f) return moved.xyz();
    const float inverse = 1.0f / moved.w;
    return {moved.x * inverse, moved.y * inverse, moved.z * inverse};
}
// A direction turned and scaled by the matrix, not moved (its w taken as 0).
[[nodiscard]] constexpr Vec3 direction(const Mat4& matrix, Vec3 along) noexcept {
    return (matrix * Vec4{along, 0.0f}).xyz();
}
// The inverse; none where the matrix has none (its determinant 0, or not a number).
[[nodiscard]] constexpr std::optional<Mat4> inverse(const Mat4& matrix) noexcept {
    const auto& m = matrix.values;
    const float s0 = m[0] * m[5] - m[4] * m[1], s1 = m[0] * m[9] - m[8] * m[1], s2 = m[0] * m[13] - m[12] * m[1];
    const float s3 = m[4] * m[9] - m[8] * m[5], s4 = m[4] * m[13] - m[12] * m[5], s5 = m[8] * m[13] - m[12] * m[9];
    const float c5 = m[10] * m[15] - m[14] * m[11], c4 = m[6] * m[15] - m[14] * m[7], c3 = m[6] * m[11] - m[10] * m[7];
    const float c2 = m[2] * m[15] - m[14] * m[3], c1 = m[2] * m[11] - m[10] * m[3], c0 = m[2] * m[7] - m[6] * m[3];
    const float determinant = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
    if (!(determinant != 0.0f) || determinant != determinant) return std::nullopt;
    const float scale = 1.0f / determinant;
    Mat4 out;
    out.values = {(m[5] * c5 - m[9] * c4 + m[13] * c3) * scale,   (-m[1] * c5 + m[9] * c2 - m[13] * c1) * scale,
                  (m[1] * c4 - m[5] * c2 + m[13] * c0) * scale,   (-m[1] * c3 + m[5] * c1 - m[9] * c0) * scale,
                  (-m[4] * c5 + m[8] * c4 - m[12] * c3) * scale,  (m[0] * c5 - m[8] * c2 + m[12] * c1) * scale,
                  (-m[0] * c4 + m[4] * c2 - m[12] * c0) * scale,  (m[0] * c3 - m[4] * c1 + m[8] * c0) * scale,
                  (m[7] * s5 - m[11] * s4 + m[15] * s3) * scale,  (-m[3] * s5 + m[11] * s2 - m[15] * s1) * scale,
                  (m[3] * s4 - m[7] * s2 + m[15] * s0) * scale,   (-m[3] * s3 + m[7] * s1 - m[11] * s0) * scale,
                  (-m[6] * s5 + m[10] * s4 - m[14] * s3) * scale, (m[2] * s5 - m[10] * s2 + m[14] * s1) * scale,
                  (-m[2] * s4 + m[6] * s2 - m[14] * s0) * scale,  (m[2] * s3 - m[6] * s1 + m[10] * s0) * scale};
    return out;
}

// Where something is, how it is turned and how large: scaled, then turned, then moved.
struct Transform {
    Vec3 translation;
    Quat rotation;
    Vec3 scale{1, 1, 1};
    [[nodiscard]] constexpr Mat4 matrix() const noexcept {
        return math::translation(translation) * math::rotation(rotation) * scaling(scale);
    }
    [[nodiscard]] constexpr Vec3 point(Vec3 at) const noexcept {
        return rotate(rotation, scaled(at, scale)) + translation;
    }
};

// The view of a camera at `eye` looking at `target`, `up` roughly up: world to camera (the camera at
// the origin, looking along −Z). Where `target` is `eye`, or `up` along the line of sight, there is no
// such view: the identity.
[[nodiscard]] inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) noexcept {
    const Vec3 forward = normalized(target - eye);
    const Vec3 right = normalized(cross(forward, up));
    if (forward == Vec3{} || right == Vec3{}) return {};
    const Vec3 above = cross(right, forward);
    Mat4 out;
    out.at(0, 0) = right.x, out.at(1, 0) = right.y, out.at(2, 0) = right.z;
    out.at(0, 1) = above.x, out.at(1, 1) = above.y, out.at(2, 1) = above.z;
    out.at(0, 2) = -forward.x, out.at(1, 2) = -forward.y, out.at(2, 2) = -forward.z;
    out.at(3, 0) = -dot(right, eye), out.at(3, 1) = -dot(above, eye), out.at(3, 2) = dot(forward, eye);
    return out;
}
// A perspective: `vertical` the whole angle of view top to bottom, `aspect` width over height; what is
// `nearest` in front of the camera is at depth 0, what is `farthest` at depth 1.
[[nodiscard]] inline Mat4 perspective(float vertical, float aspect, float nearest, float farthest) noexcept {
    const auto half = static_cast<double>(vertical) * 0.5;
    const auto focal = static_cast<float>(det::cos(half) / det::sin(half));
    Mat4 out;
    out.values = {};
    out.at(0, 0) = focal / aspect, out.at(1, 1) = focal;
    out.at(2, 2) = farthest / (nearest - farthest), out.at(2, 3) = -1.0f;
    out.at(3, 2) = nearest * farthest / (nearest - farthest);
    return out;
}
// The same with depth reversed and no far plane: `nearest` is at depth 1, what is infinitely far at 0.
// With a floating-point depth buffer this spends the format's precision where a perspective needs it
// (far away), and is what a large world is drawn with.
[[nodiscard]] inline Mat4 perspective_reversed(float vertical, float aspect, float nearest) noexcept {
    const auto half = static_cast<double>(vertical) * 0.5;
    const auto focal = static_cast<float>(det::cos(half) / det::sin(half));
    Mat4 out;
    out.values = {};
    out.at(0, 0) = focal / aspect, out.at(1, 1) = focal;
    out.at(2, 3) = -1.0f, out.at(3, 2) = nearest;
    return out;
}
// A box seen without perspective: x left … right, y bottom … top, what is `nearest` in front at depth 0,
// `farthest` at depth 1.
[[nodiscard]] constexpr Mat4 orthographic(float left, float right, float bottom, float top, float nearest,
                                          float farthest) noexcept {
    Mat4 out;
    out.at(0, 0) = 2.0f / (right - left), out.at(1, 1) = 2.0f / (top - bottom), out.at(2, 2) = 1.0f / (nearest - farthest);
    out.at(3, 0) = (left + right) / (left - right), out.at(3, 1) = (bottom + top) / (bottom - top);
    out.at(3, 2) = nearest / (nearest - farthest);
    return out;
}

}  // namespace mlibs::math
