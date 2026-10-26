// The deterministic elementary functions: accurate against correctly rounded
// references, exact on the special values, and pinned bit for bit by a hash that
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

int main(int argc, char** argv) {
    Suite s{"anim.math"};

    // Accuracy against correctly rounded values (data/math-reference.tsv,
    // made with mpmath at 256 bits), not against the platform's libm: libm's
    // last bits differ between glibc, Apple and Windows, ours do not.
    s.test("accuracy against correctly rounded references", [] {
        auto rows = reference();
        check(rows.size() == 8000, std::format("{} reference rows", rows.size()));
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
            check(w <= limit.at(name), std::format("{}: {} ulps at {:.17g}", name, w, worst_at[name]));
        }
    });

    s.test("special values", [] {
        check_bits(dm::exp(0.0), 1.0);
        check(dm::exp(1000.0) == std::numeric_limits<double>::infinity());
        check(dm::exp(-1000.0) == 0.0);
        check(std::isnan(dm::exp(std::numeric_limits<double>::quiet_NaN())));
        check_bits(dm::log(1.0), 0.0);
        check(dm::log(0.0) == -std::numeric_limits<double>::infinity());
        check(std::isnan(dm::log(-1.0)));
        check_bits(dm::sin(0.0), 0.0);
        check_bits(dm::cos(0.0), 1.0);
        check(std::isnan(dm::sin(std::numeric_limits<double>::infinity())));
        check_bits(dm::atan2(0.0, -1.0), dm::pi);
        check_bits(dm::atan2(1.0, 0.0), dm::half_pi);
        check_bits(dm::cbrt(27.0), 3.0);
        check_bits(dm::cbrt(-8.0), -2.0);
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
