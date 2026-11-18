// omui-devtools rename (D-208): a code name renamed only where it is a whole word — what is made from it
// follows (`xwidgets.inspector`, `xwidgets::`, `xwidgets-inspector`, `xwidgets_test`, a path), other words
// that contain it do not. The names are no project's: a drill renaming a real code name leaves this test.
import std;
import mcpplibs.mtest;
import omui.devtools.rename;
import omui.devtools.run;

using namespace mcpplibs::mtest;
namespace renaming = omui::devtools::rename;

static UTest cases{"devtools.rename", [] {
    "a whole word follows; a word that only contains it stays"_test = [] {
        std::size_t count = 0;
        const std::string text = "import xwidgets.inspector; xwidgets::run(); xwidgets-inspector; xwidgets_test.cpp; modules/projects/xwidgets/; myxwidgets; xwidgetsy; XWIDGETS";
        const std::string got = renaming::renamed(text, "xwidgets", "xparts", &count);
        expect(eq(got, std::string("import xparts.inspector; xparts::run(); xparts-inspector; xparts_test.cpp; modules/projects/xparts/; myxwidgets; xwidgetsy; XWIDGETS")));
        expect(eq(count, std::size_t{5}));
        expect(eq(renaming::renamed("xwidgets", "xwidgets", "xparts"), std::string("xparts"))) << "the whole text";
        const std::vector<std::string> kept{"2026-01-01-xwidgets-design.md"};
        expect(eq(renaming::renamed("see [it](2026-01-01-xwidgets-design.md) on xwidgets", "xwidgets", "xparts", nullptr, kept),
                  std::string("see [it](2026-01-01-xwidgets-design.md) on xparts"))) << "a record page's name is kept";
    };

    "a path moves whatever its content; a picture's bytes stay as they are"_test = [] {
        const auto root = std::filesystem::temp_directory_path() / std::format("devtools-rename-{}", std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root / "modules" / "xwidgets");
        std::ofstream(root / "modules" / "xwidgets" / "look.png", std::ios::binary) << "\x89PNG xwidgets";
        std::ofstream(root / "modules" / "xwidgets" / "README.md", std::ios::binary) << "# xwidgets\n";
        const auto git = [&](std::vector<std::string> words) {
            const auto ran = omui::devtools::run::program(root, "git", std::move(words));
            return ran && ran->status == 0;
        };
        if (!git({"init", "-q"}) || !git({"add", "."})) return;  // no git here: nothing to plan over
        const auto planned = renaming::plan(root, "xwidgets", "xparts");
        expect(planned.has_value()) << (planned ? "" : planned.error());
        if (!planned) return;
        const auto moved = [&](std::string_view path) {
            return std::ranges::any_of(planned->moves, [&](const auto& move) { return move.first == path; });
        };
        expect(moved("modules/xwidgets/look.png")) << "the picture moves with its directory";
        expect(moved("modules/xwidgets/README.md"));
        expect(std::ranges::none_of(planned->edits, [](const auto& edit) { return edit.first.ends_with(".png"); })) << "its bytes are not edited";
        std::error_code error;
        std::filesystem::remove_all(root, error);
    };

    "only code names: lowercase letters and digits, a letter first"_test = [] {
        expect(!renaming::plan(std::filesystem::current_path(), "XWidgets", "xparts").has_value());
        expect(!renaming::plan(std::filesystem::current_path(), "xwidgets", "x-kit").has_value());
        expect(!renaming::plan(std::filesystem::current_path(), "xwidgets", "xwidgets").has_value());
    };
}};
