// The consumer chose `sink-memory`: the constants and the branch follow.
import std;
import openmui.features;
import mlibs.testing;

using namespace mlibs::testing;
namespace f = omui::features;

static_assert(f::sink_name == "memory" && f::buffered, "the consumer's choice reached the dependency");

int main(int argc, char** argv) {
    Suite s{"features.memory"};
    s.test("the consumer's choice", [] {
        const std::array<std::string_view, 3> lines{"a", "b", "c"};
        check(f::report(lines) == "sink: memory (kept 3 lines)", f::report(lines));
    });
    return s.run(argc, argv);
}
