// The deterministic elementary functions: accurate against the platform's
// libm, exact on the special values, and pinned bit for bit by a hash that
// every platform must reproduce.
import std;
import openmui.anim;
import openmui.testing;

using namespace omui::testing;
namespace dm = omui::anim::dm;

namespace {

// Distance in units in the last place between two finite doubles.
std::int64_t ulps(double a, double b) {
    if (a == b) return 0;
    auto ia = std::bit_cast<std::int64_t>(a);
    auto ib = std::bit_cast<std::int64_t>(b);
    if (ia < 0) ia = std::numeric_limits<std::int64_t>::min() - ia;
    if (ib < 0) ib = std::numeric_limits<std::int64_t>::min() - ib;
    return ia > ib ? ia - ib : ib - ia;
}

// A fixed pseudo-random sequence (splitmix64), the same on every platform.
struct Sequence {
    std::uint64_t s;
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    double uniform(double lo, double hi) { return lo + (hi - lo) * static_cast<double>(next() >> 11) * 0x1.0p-53; }
};

template <class F, class G>
void within_ulps(F ours, G theirs, double lo, double hi, std::int64_t limit, std::uint64_t seed) {
    Sequence seq{seed};
    std::int64_t worst = 0;
    double worst_x = 0;
    for (int i = 0; i < 20000; ++i) {
        double x = seq.uniform(lo, hi);
        auto d = ulps(ours(x), theirs(x));
        if (d > worst) {
            worst = d;
            worst_x = x;
        }
    }
    check(worst <= limit, std::format("worst {} ulps at x = {:.17g}", worst, worst_x));
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"anim.math"};

    s.test("exp against libm", [] {
        within_ulps(dm::exp, [](double x) { return std::exp(x); }, -700, 700, 1, 1);
        within_ulps(dm::exp, [](double x) { return std::exp(x); }, -2, 2, 1, 2);
        check_bits(dm::exp(0.0), 1.0);
        check(dm::exp(1000.0) == std::numeric_limits<double>::infinity());
        check(dm::exp(-1000.0) == 0.0);
        check(std::isnan(dm::exp(std::numeric_limits<double>::quiet_NaN())));
    });

    s.test("log against libm", [] {
        within_ulps(dm::log, [](double x) { return std::log(x); }, 1e-300, 1e300, 1, 3);
        within_ulps(dm::log, [](double x) { return std::log(x); }, 0.5, 2, 1, 4);
        check_bits(dm::log(1.0), 0.0);
        check(dm::log(0.0) == -std::numeric_limits<double>::infinity());
        check(std::isnan(dm::log(-1.0)));
    });

    s.test("sin and cos against libm", [] {
        within_ulps(dm::sin, [](double x) { return std::sin(x); }, -100, 100, 1, 5);
        within_ulps(dm::cos, [](double x) { return std::cos(x); }, -100, 100, 1, 6);
        within_ulps(dm::sin, [](double x) { return std::sin(x); }, -0.78, 0.78, 1, 7);
        within_ulps(dm::cos, [](double x) { return std::cos(x); }, -1e5, 1e5, 1, 8);
        check_bits(dm::sin(0.0), 0.0);
        check_bits(dm::cos(0.0), 1.0);
        check(std::isnan(dm::sin(std::numeric_limits<double>::infinity())));
    });

    s.test("atan and atan2 against libm", [] {
        within_ulps(dm::atan, [](double x) { return std::atan(x); }, -50, 50, 1, 9);
        Sequence seq{10};
        std::int64_t worst = 0;
        for (int i = 0; i < 20000; ++i) {
            double y = seq.uniform(-10, 10), x = seq.uniform(-10, 10);
            worst = std::max(worst, ulps(dm::atan2(y, x), std::atan2(y, x)));
        }
        check(worst <= 2, std::format("atan2 worst {} ulps", worst));
        check_bits(dm::atan2(0.0, -1.0), dm::pi);
        check_bits(dm::atan2(1.0, 0.0), dm::half_pi);
    });

    s.test("cbrt and pow", [] {
        // fdlibm's cbrt is within 0.667 ulp of the true root; libm's own is not
        // correctly rounded either, so the two may differ by a few ulps.
        within_ulps(dm::cbrt, [](double x) { return std::cbrt(x); }, -1e6, 1e6, 4, 11);
        check_bits(dm::cbrt(27.0), 3.0);
        check_bits(dm::cbrt(-8.0), -2.0);
        Sequence seq{12};
        std::int64_t worst = 0;
        for (int i = 0; i < 20000; ++i) {
            double x = seq.uniform(0.001, 1.0), y = seq.uniform(0.2, 3.0);
            worst = std::max(worst, ulps(dm::pow(x, y), std::pow(x, y)));
        }
        check(worst <= 16, std::format("pow worst {} ulps", worst));
        check_bits(dm::pow(2.0, 10.0), 1024.0);
    });

    s.test("mix ends exactly at its endpoints", [] {
        check_bits(dm::mix(0.1, 0.7, 1.0), 0.7);
        check_bits(dm::mix(0.1, 0.7, 0.0), 0.1);
    });

    // The cross-platform gate: the same arguments give the same bits on Linux,
    // macOS and Windows. A change of this value is a change of results.
    s.test("pinned bits", [] {
        Hash h;
        Sequence seq{2026};
        for (int i = 0; i < 4096; ++i) {
            double x = seq.uniform(-20, 20);
            h.f64(dm::exp(x));
            h.f64(dm::log(dm::abs(x) + 1e-3));
            h.f64(dm::sin(x * 7.0));
            h.f64(dm::cos(x * 7.0));
            h.f64(dm::atan2(x, seq.uniform(-3, 3)));
            h.f64(dm::cbrt(x));
            h.f64(dm::pow(dm::abs(x), 1.0 / 2.4));
        }
        std::println("    anim.math pinned hash {}", h.hex());
        check_eq(h.hex(), std::string("65ba3a579a4a44b3"));
    });

    return s.run(argc, argv);
}
