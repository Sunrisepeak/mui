// The compact density as a program carries it: an overlay over whichever
// theme it is composed with.
import std;
import openmui.core;
import openmui.view;
import openmui.theme;
import openmui.theme.compact;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

int main(int argc, char** argv) {
    Suite s{"theme.compact"};
    s.test("an overlay: smaller spacing, text and radii, over any theme", [] {
        check(themes::add_compact().has_value());
        check(theme::library().overlays() == std::vector<std::string>{"compact"});
        auto base = theme::library().add("[theme.plain]\n[theme.plain.spacing]\ns = 8\n[theme.plain.text]\nbody = { size = 15 }\n[theme.plain.radius]\nmedium = 8\n",
                                          "plain.theme.omui.toml");
        check(base.has_value());
        const std::vector<std::string> layers{"plain", "compact"};
        auto t = theme::library().compose(layers);
        check(!t && t.error().find("`compact` sets the spacing `l`, which the theme under it does not have") != std::string::npos &&
                  t.error().find("did you mean") == std::string::npos,
              t ? "composed" : t.error());
    });
    return s.run(argc, argv);
}
