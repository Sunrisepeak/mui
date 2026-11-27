// omui-devtools lint: each rule catches its violation planted in a tree of its own and no other rule complains
// there; the repository itself keeps every rule; comments and literals are blanked with every line kept.
import std;
import mcpplibs.mtest;
import omui.devtools.lint;

using namespace mcpplibs::mtest;
namespace lint = omui::devtools::lint;

namespace {

std::filesystem::path root() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / ".." / ".." / ".."; }

}  // namespace

static UTest cases{"devtools.lint", [] {
    "each rule catches its planted violation, and nothing else complains there"_test = [] {
        std::ostringstream said;
        const auto scratch = std::filesystem::temp_directory_path() / "omui-devtools-lint-test";
        const int failed = lint::self_test(root(), scratch, said);
        std::filesystem::remove_all(scratch);
        expect(eq(failed, 0)) << said.str();
        expect(said.str().contains("owner:fnv1a")) << "the owners' table is read: " << said.str();
    };

    "the repository keeps every rule"_test = [] {
        const auto problems = lint::run(root());
        std::string listed;
        for (const auto& problem : problems) listed += problem.text() + "\n";
        expect(problems.empty()) << listed;
    };

    "comments and literals are blanked, every line where it was"_test = [] {
        const std::string text = "int a; // #define X\n/* a\n b */ auto s = \"mui::x\";\nauto r = R\"x(#include <unistd.h>\n)x\"; char c = '\"'; int n = 1'000;\n";
        const std::string code = lint::strip_comments(text);
        expect(eq(std::ranges::count(code, '\n'), std::ranges::count(text, '\n')));
        expect(!code.contains("#define") && !code.contains("mui::") && !code.contains("unistd")) << code;
        expect(code.contains("int a;") && code.contains("int n = 1'000;")) << code;
    };

    "a rule named alone: only its problems"_test = [] {
        const std::vector<std::string> only{"macros"};
        // What `run` keeps when a rule is named, on problems of two rules: the repository has none to keep, and
        // reading it a second time is this test's time over again (290 s of a 300 s limit on Windows, red 131).
        const auto kept = lint::select({{"macros", "a.cpp:1", "one"}, {"layout", "b", "two"}, {"macros", "c.cpp:2", "three"}}, only);
        expect(eq(kept.size(), std::size_t{2}));
        for (const auto& problem : kept) expect(eq(problem.rule, std::string("macros")));
        expect(eq(lint::rules().size(), std::size_t{46}));
    };
}};
