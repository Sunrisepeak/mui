// openmui.math: splitmix64 against its reference outputs, the ways a number
// becomes [0, 1); the elementary functions against <cmath> where the two must
// agree, and their bits pinned (the same on every platform: D-87's det).
import std;
import mlibs.math;
import mlibs.testing;

using namespace mlibs::testing;
namespace math = mlibs::math;

int main(int argc, char** argv) {
    Suite s{"math"};

    s.test("splitmix64: Vigna's reference stream; mix64 is a stream's output for its state", [] {
        math::SplitMix64 rng{1234567};
        const std::array<std::uint64_t, 5> want{6457827717110365317ull, 3203168211198807973ull, 9817491932198370423ull, 4593380528125082431ull,
                                                16408922859458223821ull};
        std::uint64_t state = 1234567;
        for (const std::uint64_t w : want) {
            check_eq(rng.next(), w);
            check_eq(math::mix64(state), w);
            state += math::golden;
        }
    });

    s.test("[0, 1): from the top 24 or 53 bits, never 1", [] {
        check(math::unit24(0) == 0.0 && math::unit53(0) == 0.0);
        check(math::unit24(~0ull) < 1.0 && math::unit53(~0ull) < 1.0);
        check(math::unit24(1ull << 63) == 0.5 && math::unit53(1ull << 63) == 0.5);
    });

    s.test("det: the functions' bits pinned, and within an ulp of <cmath> where both are exact", [] {
        Hash h;
        for (int i = -200; i <= 200; ++i) {
            const double x = i * 0.0731;
            h.f64(math::det::sin(x)).f64(math::det::cos(x)).f64(math::det::atan(x)).f64(math::det::exp(x * 0.1)).f64(math::det::cbrt(x));
            if (x > 0) h.f64(math::det::log(x)).f64(math::det::pow(x, 1.7));
        }
        std::println("    det {}", h.hex());
        check_eq(h.hex(), std::string("84bfa6c4324ae0ed"));  // GCC, LLVM, wasm: the same bits
        check(math::det::sqrt(2.0) == std::sqrt(2.0), "sqrt is the IEEE operation");
        check(std::abs(math::det::sin(0.5) - 0.479425538604203) < 1e-15 && std::abs(math::det::exp(1.0) - 2.718281828459045) < 1e-15, "sin, exp");
        check(math::det::mix(1.0, 3.0, 1.0) == 3.0, "mix at 1 is its end exactly");
    });

    s.test("gradient noise: zero on the lattice, smooth between, its slope exact, its range about ±1", [] {
        using mlibs::math::gradient_noise;
        for (const auto [x, y] : std::array<std::array<double, 2>, 4>{{{0, 0}, {3, -7}, {-12, 5}, {1000, 1000}}})
            check(gradient_noise(x, y, 7).value == 0, "zero at a lattice point");
        double lowest = 0, highest = 0;
        for (int row = 0; row < 64; ++row)
            for (int column = 0; column < 64; ++column) {
                const double x = column * 0.173 - 5, y = row * 0.211 + 2;
                const auto sample = gradient_noise(x, y, 42);
                lowest = std::min(lowest, sample.value), highest = std::max(highest, sample.value);
                constexpr double step = 1e-6;
                const double slope_x = (gradient_noise(x + step, y, 42).value - gradient_noise(x - step, y, 42).value) / (2 * step);
                const double slope_y = (gradient_noise(x, y + step, 42).value - gradient_noise(x, y - step, 42).value) / (2 * step);
                check(std::abs(slope_x - sample.dx) < 1e-6 && std::abs(slope_y - sample.dy) < 1e-6, "the slope is the derivative");
            }
        check(lowest > -1.1 && highest < 1.1 && highest - lowest > 1, std::format("range {} to {}", lowest, highest));
        check(gradient_noise(.5, .5, 1).value != gradient_noise(.5, .5, 2).value, "another seed, another field");
    });

    s.test("curl noise neither gathers nor spreads: its divergence is zero", [] {
        for (const auto [x, y] : std::array<std::array<double, 2>, 3>{{{.3, .7}, {-4.2, 9.9}, {17.5, -3.25}}}) {
            constexpr double step = 1e-5;
            const double spread = (mlibs::math::curl_noise(x + step, y, 9)[0] - mlibs::math::curl_noise(x - step, y, 9)[0]) / (2 * step) +
                (mlibs::math::curl_noise(x, y + step, 9)[1] - mlibs::math::curl_noise(x, y - step, 9)[1]) / (2 * step);
            check(std::abs(spread) < 1e-5, std::format("divergence {} at ({}, {})", spread, x, y));
        }
    });

    s.test("noise is the same bits on every platform", [] {
        // Pinned on x86-64 Linux; every CI platform must reproduce them.
        check(mlibs::math::gradient_noise(.37, 1.91, 2026).value == -0x1.e0f11f2e3f4b7p-4, std::format("{:a}", mlibs::math::gradient_noise(.37, 1.91, 2026).value));
        check(mlibs::math::curl_noise(-8.125, 3.5, 11)[0] == -0x1.9dc4bb5631ef3p-2, std::format("{:a}", mlibs::math::curl_noise(-8.125, 3.5, 11)[0]));
    });
    return s.run(argc, argv);
}
