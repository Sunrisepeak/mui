// openmui.math: splitmix64 against its reference outputs, the ways a number
// becomes [0, 1); the elementary functions against <cmath> where the two must
// agree, and their bits pinned (the same on every platform: D-87's det).
import std;
import openmui.math;
import openmui.testing;

using namespace omui::testing;
namespace math = omui::math;

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

    return s.run(argc, argv);
}
