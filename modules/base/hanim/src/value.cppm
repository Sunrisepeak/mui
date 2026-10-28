// hanim:value — what can be animated, and how a value is interpolated.
//
// A type is animatable when it can be taken apart into a fixed number of
// double "lanes" and put back together (`lanes<T>`). Every primitive in this
// package — tweens, keyframes, springs, decay, timelines — works on lanes, so
// a spring on a colour or a transform is the same code as a spring on a
// number, and a program adds its own types by specialising `lanes`.
//
// The lanes are chosen so that a straight line between two lane vectors is the
// interpolation people expect: colours travel through OKLab (perceptually even,
// no muddy midpoints) with premultiplied alpha, transforms through their
// decomposed parts (no shearing midway through a rotation).
export module hanim:value;

import std;
import :math;

export namespace hanim {

template <class T>
struct lanes;  // specialise: static constexpr int count; to(const T&) -> std::array<double, count>; from(...) -> T

template <class T>
concept Animatable = requires(const T& v, const std::array<double, lanes<T>::count>& a) {
    { lanes<T>::to(v) } -> std::same_as<std::array<double, lanes<T>::count>>;
    { lanes<T>::from(a) } -> std::same_as<T>;
};

template <Animatable T>
using lanes_of = std::array<double, lanes<T>::count>;

// a at t = 0, b at t = 1 (both exactly, whatever the lane conversion rounds),
// and the straight line between their lanes in between. t outside [0, 1]
// extrapolates, which overshooting curves rely on.
template <Animatable T>
[[nodiscard]] T lerp(const T& a, const T& b, double t) {
    if (t == 0.0) return a;
    if (t == 1.0) return b;
    auto la = lanes<T>::to(a);
    auto lb = lanes<T>::to(b);
    lanes_of<T> out{};
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = dm::mix(la[i], lb[i], t);
    return lanes<T>::from(out);
}

// ── numbers ────────────────────────────────────────────────────────────────

template <>
struct lanes<double> {
    static constexpr int count = 1;
    static constexpr std::array<double, 1> to(const double& v) noexcept { return {v}; }
    static constexpr double from(const std::array<double, 1>& a) noexcept { return a[0]; }
};

template <>
struct lanes<float> {
    static constexpr int count = 1;
    static constexpr std::array<double, 1> to(const float& v) noexcept { return {static_cast<double>(v)}; }
    static constexpr float from(const std::array<double, 1>& a) noexcept { return static_cast<float>(a[0]); }
};

// Integers round to nearest (half away from zero), so a counter animated from
// 0 to 10 shows every integer once.
template <std::integral I>
    requires(!std::same_as<I, bool>)
struct lanes<I> {
    static constexpr int count = 1;
    static constexpr std::array<double, 1> to(const I& v) noexcept { return {static_cast<double>(v)}; }
    static constexpr I from(const std::array<double, 1>& a) noexcept {
        double v = a[0];
        // Truncation of |v| + 0.5 is the floor of it, since it is positive.
        return v < 0 ? static_cast<I>(-static_cast<long long>(-v + 0.5)) : static_cast<I>(static_cast<long long>(v + 0.5));
    }
};

template <std::size_t N>
struct lanes<std::array<double, N>> {
    static constexpr int count = static_cast<int>(N);
    static constexpr std::array<double, N> to(const std::array<double, N>& v) noexcept { return v; }
    static constexpr std::array<double, N> from(const std::array<double, N>& a) noexcept { return a; }
};

// ── vectors ────────────────────────────────────────────────────────────────

struct Vec2 {
    double x = 0, y = 0;
    // Written out: GCC 16.1 crashes on a defaulted comparison here once an importer uses it.
    friend constexpr bool operator==(const Vec2& a, const Vec2& b) noexcept { return a.x == b.x && a.y == b.y; }
};
struct Vec3 {
    double x = 0, y = 0, z = 0;
    // Written out: GCC 16.1 crashes on a defaulted comparison here once an importer uses it.
    friend constexpr bool operator==(const Vec3& a, const Vec3& b) noexcept { return a.x == b.x && a.y == b.y && a.z == b.z; }
};
struct Vec4 {
    double x = 0, y = 0, z = 0, w = 0;
    // Written out: GCC 16.1 crashes on a defaulted comparison here once an importer uses it.
    friend constexpr bool operator==(const Vec4& a, const Vec4& b) noexcept { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
};

template <>
struct lanes<Vec2> {
    static constexpr int count = 2;
    static constexpr std::array<double, 2> to(const Vec2& v) noexcept { return {v.x, v.y}; }
    static constexpr Vec2 from(const std::array<double, 2>& a) noexcept { return {a[0], a[1]}; }
};
template <>
struct lanes<Vec3> {
    static constexpr int count = 3;
    static constexpr std::array<double, 3> to(const Vec3& v) noexcept { return {v.x, v.y, v.z}; }
    static constexpr Vec3 from(const std::array<double, 3>& a) noexcept { return {a[0], a[1], a[2]}; }
};
template <>
struct lanes<Vec4> {
    static constexpr int count = 4;
    static constexpr std::array<double, 4> to(const Vec4& v) noexcept { return {v.x, v.y, v.z, v.w}; }
    static constexpr Vec4 from(const std::array<double, 4>& a) noexcept { return {a[0], a[1], a[2], a[3]}; }
};

// ── colour ─────────────────────────────────────────────────────────────────

// An sRGB colour with straight (not premultiplied) alpha, every channel in
// [0, 1] — the form designers and CSS write.
struct Rgba {
    float r = 0, g = 0, b = 0, a = 1;
    // Written out: GCC 16.1 crashes on a defaulted comparison here once an importer uses it.
    friend constexpr bool operator==(const Rgba& a, const Rgba& b) noexcept { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

    // #rgb, #rgba, #rrggbb, #rrggbbaa
    [[nodiscard]] static std::optional<Rgba> parse_hex(std::string_view hex) noexcept;
    [[nodiscard]] static constexpr Rgba bytes(int r, int g, int b, int a = 255) noexcept {
        return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f,
                static_cast<float>(a) / 255.0f};
    }
};

struct OkLab {
    double l = 0, a = 0, b = 0;
};

[[nodiscard]] double srgb_to_linear(double c) noexcept;
[[nodiscard]] double linear_to_srgb(double c) noexcept;
[[nodiscard]] OkLab to_oklab(const Rgba& c) noexcept;
// Out-of-gamut results are clipped to [0, 1] per channel.
[[nodiscard]] Rgba from_oklab(const OkLab& lab, float alpha) noexcept;

// Lanes: OKLab premultiplied by alpha, then alpha — so a fade to transparent
// does not pass through the transparent colour's hue.
template <>
struct lanes<Rgba> {
    static constexpr int count = 4;
    static std::array<double, 4> to(const Rgba& c) noexcept {
        auto lab = to_oklab(c);
        double a = c.a;
        return {lab.l * a, lab.a * a, lab.b * a, a};
    }
    static Rgba from(const std::array<double, 4>& v) noexcept {
        double a = dm::clamp(v[3], 0.0, 1.0);
        if (a <= 0.0) return {0, 0, 0, 0};
        return from_oklab({v[0] / v[3], v[1] / v[3], v[2] / v[3]}, static_cast<float>(a));
    }
};

// ── 2D transform ───────────────────────────────────────────────────────────

// A 2D transform in decomposed form: scale, then skew, then rotate, then
// translate, about an origin the consumer applies. Interpolating the parts
// keeps a rotating, scaling element rigid in between.
struct Transform2D {
    double tx = 0, ty = 0;
    double sx = 1, sy = 1;
    double rotate = 0;  // radians, clockwise in a y-down space
    double skew_x = 0;  // radians
    // Written out: GCC 16.1 crashes on a defaulted comparison here once an importer uses it.
    friend constexpr bool operator==(const Transform2D& a, const Transform2D& b) noexcept { return a.tx == b.tx && a.ty == b.ty && a.sx == b.sx && a.sy == b.sy && a.rotate == b.rotate && a.skew_x == b.skew_x; }

    // The affine matrix [a c e; b d f] of x' = a·x + c·y + e, y' = b·x + d·y + f.
    [[nodiscard]] std::array<double, 6> matrix() const noexcept;
};

template <>
struct lanes<Transform2D> {
    static constexpr int count = 6;
    static constexpr std::array<double, 6> to(const Transform2D& t) noexcept {
        return {t.tx, t.ty, t.sx, t.sy, t.rotate, t.skew_x};
    }
    static constexpr Transform2D from(const std::array<double, 6>& a) noexcept {
        return {a[0], a[1], a[2], a[3], a[4], a[5]};
    }
};

}  // namespace hanim
