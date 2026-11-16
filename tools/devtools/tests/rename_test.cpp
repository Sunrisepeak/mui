// omui-devtools rename (D-208): a code name renamed only where it is a whole word — what is made from it follows
// (`xtools.inspector`, `xtools::`, `xtools-inspector`, `xtools_test`, a path), other words that contain it do not.
import std;
import mcpplibs.mtest;
import omui.devtools.rename;

using namespace mcpplibs::mtest;
namespace renaming = omui::devtools::rename;

static UTest cases{"devtools.rename", [] {
    "a whole word follows; a word that only contains it stays"_test = [] {
        std::size_t count = 0;
        const std::string text = "import xtools.inspector; xtools::run(); xtools-inspector; xtools_test.cpp; modules/projects/xtools/; myxtools; xtoolsy; XTOOLS";
        const std::string got = renaming::renamed(text, "xtools", "xkit", &count);
        expect(eq(got, std::string("import xkit.inspector; xkit::run(); xkit-inspector; xkit_test.cpp; modules/projects/xkit/; myxtools; xtoolsy; XTOOLS")));
        expect(eq(count, std::size_t{5}));
        expect(eq(renaming::renamed("xtools", "xtools", "xkit"), std::string("xkit"))) << "the whole text";
    };

    "only code names: lowercase letters and digits, a letter first"_test = [] {
        expect(!renaming::plan(std::filesystem::current_path(), "XTools", "xkit").has_value());
        expect(!renaming::plan(std::filesystem::current_path(), "xtools", "x-kit").has_value());
        expect(!renaming::plan(std::filesystem::current_path(), "xtools", "xtools").has_value());
    };
}};
