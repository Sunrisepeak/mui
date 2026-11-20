// The deterministic elementary functions: accurate against correctly rounded
// references, exact on the special values, and pinned bit for bit by a hash that
// every platform must reproduce.
import std;
import hanim.core;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace dm = hanim::dm;

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
        s += mlibs::math::golden;
        return mlibs::math::finalize(s);
    }
    double uniform(double lo, double hi) { return lo + (hi - lo) * static_cast<double>(next() >> 11) * 0x1.0p-53; }
};

struct Reference {
    std::string name;
    double      a = 0, b = 0, expected = 0;
};

double hexfloat(std::string_view s) {
    bool negative = s.starts_with('-');
    if (negative) s.remove_prefix(1);
    if (s.starts_with("0x")) s.remove_prefix(2);
    double v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v, std::chars_format::hex);
    return negative ? -v : v;
}

std::vector<Reference> reference() {
    auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path() / "data" /
                "math-reference.tsv";
    std::ifstream in(file);
    std::vector<Reference> rows;
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string_view> f;
        std::string_view rest = line;
        while (true) {
            auto tab = rest.find('\t');
            f.push_back(rest.substr(0, tab));
            if (tab == std::string_view::npos) break;
            rest.remove_prefix(tab + 1);
        }
        if (f.size() == 3) rows.push_back({std::string(f[0]), hexfloat(f[1]), 0, hexfloat(f[2])});
        else if (f.size() == 4) rows.push_back({std::string(f[0]), hexfloat(f[1]), hexfloat(f[2]), hexfloat(f[3])});
    }
    return rows;
}

}  // namespace

UTest cases{"hanim.math", [] {

    // Accuracy against correctly rounded values (data/math-reference.tsv,
    // made with mpmath at 256 bits), not against the platform's libm: libm's
    // last bits differ between glibc, Apple and Windows, ours do not.
    "accuracy against correctly rounded references"_test = [] {
        auto rows = reference();
        expect(rows.size() == 8000) << std::format("{} reference rows", rows.size());
        std::map<std::string, std::int64_t> worst;
        std::map<std::string, double> worst_at;
        for (const auto& r : rows) {
            double got = 0;
            if (r.name == "exp") got = dm::exp(r.a);
            else if (r.name == "log") got = dm::log(r.a);
            else if (r.name == "sin") got = dm::sin(r.a);
            else if (r.name == "cos") got = dm::cos(r.a);
            else if (r.name == "atan") got = dm::atan(r.a);
            else if (r.name == "atan2") got = dm::atan2(r.a, r.b);
            else if (r.name == "cbrt") got = dm::cbrt(r.a);
            else if (r.name == "pow") got = dm::pow(r.a, r.b);
            auto d = ulps(got, r.expected);
            if (d > worst[r.name]) {
                worst[r.name] = d;
                worst_at[r.name] = r.a;
            }
        }
        // fdlibm's bounds: below one ulp for each function but pow, which is
        // exp(y·log x) and loses a few bits for these magnitudes.
        const std::map<std::string, std::int64_t> limit{{"exp", 1}, {"log", 1}, {"sin", 1}, {"cos", 1},
                                                        {"atan", 1}, {"atan2", 2}, {"cbrt", 1}, {"pow", 16}};
        for (const auto& [name, w] : worst) {
            std::println("    {:6} worst {} ulp", name, w);
            expect(w <= limit.at(name)) << std::format("{}: {} ulps at {:.17g}", name, w, worst_at[name]);
        }
    };

    "special values"_test = [] {
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::exp(0.0))), std::bit_cast<std::uint64_t>(double(1.0))));
        expect(dm::exp(1000.0) == std::numeric_limits<double>::infinity());
        expect(dm::exp(-1000.0) == 0.0);
        expect(std::isnan(dm::exp(std::numeric_limits<double>::quiet_NaN())));
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::log(1.0))), std::bit_cast<std::uint64_t>(double(0.0))));
        expect(dm::log(0.0) == -std::numeric_limits<double>::infinity());
        expect(std::isnan(dm::log(-1.0)));
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::sin(0.0))), std::bit_cast<std::uint64_t>(double(0.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::cos(0.0))), std::bit_cast<std::uint64_t>(double(1.0))));
        expect(std::isnan(dm::sin(std::numeric_limits<double>::infinity())));
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::atan2(0.0, -1.0))), std::bit_cast<std::uint64_t>(double(dm::pi))));
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::atan2(1.0, 0.0))), std::bit_cast<std::uint64_t>(double(dm::half_pi))));
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::cbrt(27.0))), std::bit_cast<std::uint64_t>(double(3.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::cbrt(-8.0))), std::bit_cast<std::uint64_t>(double(-2.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::pow(2.0, 10.0))), std::bit_cast<std::uint64_t>(double(1024.0))));
    };

    "mix ends exactly at its endpoints"_test = [] {
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::mix(0.1, 0.7, 1.0))), std::bit_cast<std::uint64_t>(double(0.7))));
        expect(eq(std::bit_cast<std::uint64_t>(double(dm::mix(0.1, 0.7, 0.0))), std::bit_cast<std::uint64_t>(double(0.1))));
    };

    // The cross-platform gate: the same arguments give the same bits on Linux,
    // macOS and Windows. A change of this value is a change of results.
    "pinned bits"_test = [] {
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
        expect(eq(h.hex(), std::string("65ba3a579a4a44b3")));
    };
}};
