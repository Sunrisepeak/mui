// omui-devtools incremental: the build it asks mcpp for, the configurations a
// build's events name, the outputs a compilation database names, and which of
// them changed between two looks.
import std;
import mcpplibs.mtest;
import omui.devtools.incremental;

using namespace mcpplibs::mtest;
namespace check = omui::devtools::incremental;
namespace fs = std::filesystem;

static UTest cases{"devtools.incremental", [] {
    "the build: the profile, the selection, what is left out, the toolchain, locked"_test = [] {
        const auto args = check::arguments({.workspace = true, .excluded = {"omui.reflect"}, .profile = "ci", .toolchain = "llvm@23.1.3", .locked = true});
        expect(args == std::vector<std::string>{"build", "--profile", "ci", "--message-format", "json", "--workspace", "--exclude", "omui.reflect", "--toolchain", "llvm@23.1.3", "--locked"});
        expect(check::arguments({.packages = {"a", "b"}}) == std::vector<std::string>{"build", "--profile", "dev", "--message-format", "json", "-p", "a", "-p", "b"});
    };

    "the configurations a build's events name: its plan events, other lines passed over"_test = [] {
        const auto found = check::configurations(
            "{\"event\":\"plan\",\"configuration\":{\"outputDir\":\"/w/target/x/aaa\"}}\n"
            "not json\n"
            "{\"event\":\"compile\",\"configuration\":{\"outputDir\":\"/w/target/x/zzz\"}}\n"
            "{\"event\":\"plan\",\"configuration\":{\"outputDir\":\"/w/target/x/bbb\"}}\n"
            "{\"event\":\"plan\",\"configuration\":{\"outputDir\":\"/w/target/x/aaa\"}}");
        expect(found == std::set<fs::path>{"/w/target/x/aaa", "/w/target/x/bbb"});
    };

    "the outputs a database names, relative ones from its directory; a missing one counted apart; a touch is a change"_test = [] {
        const fs::path directory = fs::temp_directory_path() / std::format("omui-devtools-incremental-{}", std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(directory / "obj");
        std::ofstream(directory / "obj/a.o") << "a";
        std::ofstream(directory / "obj/b.o") << "bb";
        const std::string where = directory.generic_string();
        std::ofstream(directory / "compile_commands.json") << std::format(
            R"([{{"directory":"{0}","file":"a.cpp","output":"obj/a.o"}},{{"directory":"/","file":"b.cpp","output":"{0}/obj/b.o"}},{{"directory":"{0}","file":"c.cpp","output":"obj/c.o"}}])", where);
        const auto before = check::outputs({directory});
        expect(fatal(before.has_value())) << (before ? "" : before.error());
        expect(eq(before->found.size(), std::size_t{2}) && eq(before->missing.size(), std::size_t{1}));
        const auto same = check::outputs({directory});
        expect(fatal(same.has_value()));
        expect(check::changed(*before, *same).empty());
        fs::last_write_time(directory / "obj/b.o", fs::last_write_time(directory / "obj/b.o") + std::chrono::seconds(5));
        const auto after = check::outputs({directory});
        const auto touched = check::changed(*before, *after);
        expect(fatal(eq(touched.size(), std::size_t{1}))) << touched.size();
        expect(touched.front().ends_with("obj/b.o"));
        std::ofstream(directory / "compile_commands.json") << "[]";
        expect(!check::outputs({directory}).has_value()) << "no output at all is no check";
        fs::remove_all(directory);
    };
}};
