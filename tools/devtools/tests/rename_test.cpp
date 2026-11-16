// omui-devtools rename (D-208): a code name renamed only where it is a whole word — what is made from it
// follows (`xwidgets.inspector`, `xwidgets::`, `xwidgets-inspector`, `xwidgets_test`, a path), other words
// that contain it do not. The names are no project's: a drill renaming a real code name leaves this test.
import std;
import mcpplibs.mtest;
import omui.devtools.rename;

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
    };

    "only code names: lowercase letters and digits, a letter first"_test = [] {
        expect(!renaming::plan(std::filesystem::current_path(), "XWidgets", "xparts").has_value());
        expect(!renaming::plan(std::filesystem::current_path(), "xwidgets", "x-kit").has_value());
        expect(!renaming::plan(std::filesystem::current_path(), "xwidgets", "xwidgets").has_value());
    };
}};
