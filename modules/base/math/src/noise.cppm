// mlibs.math.api:noise — coherent noise that is the same everywhere: 2D
// gradient noise (Perlin's, with his quintic fade) and its curl, a swirling
// field without sources or sinks. Lattice gradients come from mix64 of the
// cell and a seed, one of sixteen exact directions; the arithmetic is the
// five correctly rounded operations and floor, so every platform gives the
// same bits. Animation (path noise), simulation (force fields) and
// materials share it (convergence plan §6.9).
//
//     const auto sample = mlibs::math::gradient_noise(x, y, seed);   // value in about [-1, 1], and
//     its slope const auto [across, down] = mlibs::math::curl_noise(x, y, seed);
export module mlibs.math.api:noise;

import std;
import :random;

export namespace mlibs::math {

struct NoiseSample {
    double value = 0;       // about −1 to 1
    double dx = 0, dy = 0;  // its partial derivatives
};

namespace detail {
// cos and sin of k·π/8, k = 0 … 15.
inline constexpr std::array<std::array<double, 2>, 16> noise_directions{{
    {1, 0},
    {0.9238795325112867, 0.3826834323650898},
    {0.7071067811865476, 0.7071067811865476},
    {0.3826834323650898, 0.9238795325112867},
    {0, 1},
    {-0.3826834323650898, 0.9238795325112867},
    {-0.7071067811865476, 0.7071067811865476},
    {-0.9238795325112867, 0.3826834323650898},
    {-1, 0},
    {-0.9238795325112867, -0.3826834323650898},
    {-0.7071067811865476, -0.7071067811865476},
    {-0.3826834323650898, -0.9238795325112867},
    {0, -1},
    {0.3826834323650898, -0.9238795325112867},
    {0.7071067811865476, -0.7071067811865476},
    {0.9238795325112867, -0.3826834323650898},
}};
inline const std::array<double, 2>& lattice_direction(std::int64_t cell_x, std::int64_t cell_y,
                                                      std::uint64_t seed) noexcept {
    const auto key = mix64(static_cast<std::uint64_t>(cell_x) * 0xd6e8feb86659fd93ull ^
                           mix64(static_cast<std::uint64_t>(cell_y) ^ seed));
    return noise_directions[key >> 60];
}
}  // namespace detail

// 2D gradient noise at (x, y): zero at every lattice point, smooth (C2)
// between, its derivatives exact. Coordinates beyond ±2⁵² lose their
// fraction and give zero.
[[nodiscard]] inline NoiseSample gradient_noise(double x, double y, std::uint64_t seed) noexcept {
    if (!(std::abs(x) < 4503599627370496.0) || !(std::abs(y) < 4503599627370496.0))
        return {};
    const double floor_x = std::floor(x), floor_y = std::floor(y);
    const double fx = x - floor_x, fy = y - floor_y;
    const auto cell_x = static_cast<std::int64_t>(floor_x),
               cell_y = static_cast<std::int64_t>(floor_y);
    const auto& g00 = detail::lattice_direction(cell_x, cell_y, seed);
    const auto& g10 = detail::lattice_direction(cell_x + 1, cell_y, seed);
    const auto& g01 = detail::lattice_direction(cell_x, cell_y + 1, seed);
    const auto& g11 = detail::lattice_direction(cell_x + 1, cell_y + 1, seed);
    const double a = g00[0] * fx + g00[1] * fy;
    const double b = g10[0] * (fx - 1) + g10[1] * fy;
    const double c = g01[0] * fx + g01[1] * (fy - 1);
    const double d = g11[0] * (fx - 1) + g11[1] * (fy - 1);
    const auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    const auto slope = [](double t) { return 30 * t * t * (t - 1) * (t - 1); };
    const double u = fade(fx), v = fade(fy), du = slope(fx), dv = slope(fy);
    const double mixed = a - b - c + d;
    // Unit gradients reach about ±0.7 in 2D; √2 brings the range to about ±1.
    constexpr double range = 1.4142135623730951;
    NoiseSample sample;
    sample.value = range * (a + u * (b - a) + v * (c - a) + u * v * mixed);
    sample.dx = range * (g00[0] + u * (g10[0] - g00[0]) + v * (g01[0] - g00[0]) +
                         u * v * (g00[0] - g10[0] - g01[0] + g11[0]) + du * ((b - a) + v * mixed));
    sample.dy = range * (g00[1] + u * (g10[1] - g00[1]) + v * (g01[1] - g00[1]) +
                         u * v * (g00[1] - g10[1] - g01[1] + g11[1]) + dv * ((c - a) + u * mixed));
    return sample;
}

// The curl of gradient noise as a stream function: (∂ψ/∂y, −∂ψ/∂x), a field
// that swirls and neither gathers nor spreads.
[[nodiscard]] inline std::array<double, 2> curl_noise(double x, double y,
                                                      std::uint64_t seed) noexcept {
    const auto stream = gradient_noise(x, y, seed);
    return {stream.dy, -stream.dx};
}

}  // namespace mlibs::math
