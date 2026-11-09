// The consumer chose `sink-memory`: the constants and the branch follow.
import std;
import omui.features;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace f = omui::features;

static_assert(f::sink_name == "memory" && f::buffered, "the consumer's choice reached the dependency");

UTest cases{"features.memory", [] {
    "the consumer's choice"_test = [] {
        const std::array<std::string_view, 3> lines{"a", "b", "c"};
        expect(f::report(lines) == "sink: memory (kept 3 lines)") << f::report(lines);
    };
}};
