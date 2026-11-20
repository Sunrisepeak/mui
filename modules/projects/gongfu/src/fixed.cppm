// gongfu:fixed — the simulation's numbers: fixed point, the same bits on
// every machine (D-85). A Fixed is 16 integer and 16 fractional bits in 32
// (Q16.16): ±32767 units in steps of 1/65536. Products and quotients go
// through 64 bits; a product is floored (an arithmetic shift), a quotient
// truncated toward zero, as C++ defines them, so no compiler, flag or CPU
// changes a result. Nothing in the simulation is a float; to_double() is for
// what draws it.
export module gongfu:fixed;

import std;

export namespace gongfu {

class Fixed {
public:
    static constexpr int fraction_bits = 16;
    static constexpr std::int32_t one_bits = 1 << fraction_bits;

    constexpr Fixed() noexcept = default;

    [[nodiscard]] static constexpr Fixed raw(std::int32_t bits) noexcept {
        Fixed f;
        f.bits_ = bits;
        return f;
    }
    [[nodiscard]] static constexpr Fixed of(std::int32_t whole) noexcept { return raw(whole * one_bits); }
    // num / den, truncated toward zero.
    [[nodiscard]] static constexpr Fixed ratio(std::int32_t num, std::int32_t den) noexcept {
        return raw(static_cast<std::int32_t>((static_cast<std::int64_t>(num) << fraction_bits) / den));
    }

    [[nodiscard]] constexpr std::int32_t bits() const noexcept { return bits_; }
    // The whole part, floored.
    [[nodiscard]] constexpr std::int32_t floor() const noexcept { return bits_ >> fraction_bits; }
    // For drawing only.
    [[nodiscard]] constexpr double to_double() const noexcept { return static_cast<double>(bits_) / one_bits; }

    constexpr Fixed operator-() const noexcept { return raw(-bits_); }
    constexpr Fixed& operator+=(Fixed o) noexcept { return bits_ += o.bits_, *this; }
    constexpr Fixed& operator-=(Fixed o) noexcept { return bits_ -= o.bits_, *this; }
    friend constexpr Fixed operator+(Fixed a, Fixed b) noexcept { return raw(a.bits_ + b.bits_); }
    friend constexpr Fixed operator-(Fixed a, Fixed b) noexcept { return raw(a.bits_ - b.bits_); }
    friend constexpr Fixed operator*(Fixed a, Fixed b) noexcept {
        return raw(static_cast<std::int32_t>((static_cast<std::int64_t>(a.bits_) * b.bits_) >> fraction_bits));
    }
    friend constexpr Fixed operator/(Fixed a, Fixed b) noexcept {
        return raw(static_cast<std::int32_t>((static_cast<std::int64_t>(a.bits_) << fraction_bits) / b.bits_));
    }
    // By a whole number (a count of frames, a percentage's numerator).
    friend constexpr Fixed operator*(Fixed a, std::int32_t n) noexcept { return raw(a.bits_ * n); }
    friend constexpr Fixed operator/(Fixed a, std::int32_t n) noexcept { return raw(a.bits_ / n); }
    // Written out: GCC 16 crashes importing a defaulted one (an internal compiler error).
    friend constexpr bool operator==(Fixed a, Fixed b) noexcept { return a.bits_ == b.bits_; }
    friend constexpr bool operator<(Fixed a, Fixed b) noexcept { return a.bits_ < b.bits_; }
    friend constexpr bool operator>(Fixed a, Fixed b) noexcept { return a.bits_ > b.bits_; }
    friend constexpr bool operator<=(Fixed a, Fixed b) noexcept { return a.bits_ <= b.bits_; }
    friend constexpr bool operator>=(Fixed a, Fixed b) noexcept { return a.bits_ >= b.bits_; }

private:
    std::int32_t bits_ = 0;
};

// A literal written in the source, converted once, at compile time:
// 2.5_fx is Fixed::raw(163840). Rounded to the nearest step.
consteval Fixed operator""_fx(long double v) {
    const long double scaled = v * Fixed::one_bits;
    return Fixed::raw(static_cast<std::int32_t>(scaled < 0 ? scaled - 0.5L : scaled + 0.5L));
}
consteval Fixed operator""_fx(unsigned long long v) { return Fixed::of(static_cast<std::int32_t>(v)); }

[[nodiscard]] constexpr Fixed abs(Fixed f) noexcept { return f < Fixed{} ? -f : f; }
[[nodiscard]] constexpr Fixed min(Fixed a, Fixed b) noexcept { return a < b ? a : b; }
[[nodiscard]] constexpr Fixed max(Fixed a, Fixed b) noexcept { return a < b ? b : a; }
[[nodiscard]] constexpr Fixed clamp(Fixed v, Fixed lo, Fixed hi) noexcept { return v < lo ? lo : hi < v ? hi : v; }

// A point or a velocity: x along the stage, y up from the ground.
struct Vec {
    Fixed x, y;
    friend constexpr Vec operator+(Vec a, Vec b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec operator-(Vec a, Vec b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend constexpr bool operator==(Vec a, Vec b) noexcept { return a.x == b.x && a.y == b.y; }
};

// A rectangle in a fighter's own space: x forward from its origin (the way
// it faces), y up from its feet; `x`, `y` its near-bottom corner.
struct Box {
    Fixed x, y, w, h;
    friend constexpr bool operator==(Box a, Box b) noexcept { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
};

// A rectangle on the stage: left, bottom, right, top.
struct Rect {
    Fixed left, bottom, right, top;
    [[nodiscard]] constexpr bool overlaps(const Rect& o) const noexcept {
        return left < o.right && o.left < right && bottom < o.top && o.bottom < top;
    }
    friend constexpr bool operator==(Rect a, Rect b) noexcept {
        return a.left == b.left && a.bottom == b.bottom && a.right == b.right && a.top == b.top;
    }
};

// A box placed on the stage for a fighter at `at` facing `facing` (+1 right,
// −1 left): mirrored when it faces left.
[[nodiscard]] constexpr Rect place(const Box& b, Vec at, std::int32_t facing) noexcept {
    const Fixed near = facing > 0 ? at.x + b.x : at.x - b.x - b.w;
    return {near, at.y + b.y, near + b.w, at.y + b.y + b.h};
}

}  // namespace gongfu
