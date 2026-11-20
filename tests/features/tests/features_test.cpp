// Whichever feature chose the sink, the constants agree with it and the code
// took the matching branch at compile time.
import std;
import omui.features;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace f = omui::features;

static_assert(f::sink_name == "stdout" || f::sink_name == "memory");
static_assert(f::buffered == (f::sink_name == "memory"));

UTest cases{"features", [] {
    "the chosen partition, and the branch it chose"_test = [] {
        const std::array<std::string_view, 2> lines{"one", "two"};
        const std::string r = f::report(lines);
        std::println("{}", r.substr(0, r.find('\n')));  // the CI script reads which sink was built
        if constexpr (f::buffered) expect(r == "sink: memory (kept 2 lines)") << r;
        else expect(r == "sink: stdout\n  one\n  two") << r;
    };
}};
