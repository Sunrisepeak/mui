// openmui.math:det — elementary functions with the same bits on every platform
// (D-87: moved from hanim:math, whose hanim::dm is now this namespace).
//
// WHY NOT <cmath>
//
// `std::exp`, `std::sin`, `std::pow` … are not specified to the last bit, and
// glibc, Apple's libm and the Windows UCRT give different last bits for the
// same argument. A spring evaluated at t = 0.35 s would then differ between a
// video rendered on Linux and the same video rendered on macOS, and so would
// every frame after a colour conversion. Animation results are part of the
// conformance suite (A level: identical bits), so the functions animation uses
// are implemented here, from the fdlibm algorithms, with nothing but the five
// IEEE-754 operations that are correctly rounded everywhere (+ − × ÷ √).
//
// The package is compiled with `-ffp-contract=off` (no fused multiply-add the
// source did not write) and without fast-math, which is what keeps those five
// operations exactly what the source says.
//
// Accuracy is fdlibm's: below one ulp for exp, log, sin, cos, atan, atan2 and
// cbrt; `pow` is exp(y·log x) and is accurate to a few ulp for the magnitudes
// animation uses (gamma curves, easing exponents).
export module openmui.math:det;

import std;

export namespace omui::math::det {

inline constexpr double pi      = 3.14159265358979311600e+00;
inline constexpr double two_pi  = 6.28318530717958623200e+00;
inline constexpr double half_pi = 1.57079632679489655800e+00;
inline constexpr double ln2     = 6.93147180559945286227e-01;

[[nodiscard]] double exp(double x) noexcept;
[[nodiscard]] double log(double x) noexcept;
[[nodiscard]] double sin(double x) noexcept;
[[nodiscard]] double cos(double x) noexcept;
[[nodiscard]] double atan(double x) noexcept;
[[nodiscard]] double atan2(double y, double x) noexcept;
[[nodiscard]] double cbrt(double x) noexcept;

// x^y for x ≥ 0 (a negative base yields NaN, as the curves that use it need).
[[nodiscard]] double pow(double x, double y) noexcept;

// √x is one of the correctly rounded IEEE operations, so the standard one is
// already identical everywhere; it is named here so animation code reads from
// one namespace.
[[nodiscard]] inline double sqrt(double x) noexcept { return std::sqrt(x); }

[[nodiscard]] constexpr double abs(double x) noexcept { return x < 0 ? -x : (x == 0 ? 0.0 : x); }

[[nodiscard]] constexpr double clamp(double x, double lo, double hi) noexcept {
    return x < lo ? lo : (x > hi ? hi : x);
}

// a + (b − a)·t, written out so that no library implementation of `std::lerp`
// (which libstdc++ and libc++ compute differently) decides the last bit. t = 1
// returns b exactly, so an animation that has ended holds its target value.
[[nodiscard]] constexpr double mix(double a, double b, double t) noexcept {
    if (t == 1.0) return b;
    return a + (b - a) * t;
}

}  // namespace omui::math::det
