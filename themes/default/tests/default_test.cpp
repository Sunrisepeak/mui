// The default theme as a program carries it: add_default() puts light, dark
// and reduced_motion in the program's library, light and dark are the
// runtime's built-in tokens, and adding again changes nothing.
import std;
import openmui.core;
import openmui.view;
import openmui.theme;
import openmui.theme.default_theme;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

int main(int argc, char** argv) {
    Suite s{"theme.default"};
    s.test("the embedded file: light, dark and reduced motion, the built-in tokens", [] {
        auto added = themes::add_default();
        check(added.has_value(), added ? "" : added.error()[0].text());
        check(themes::add_default().has_value(), "again: the same file, the same themes");
        check(theme::library().themes() == std::vector<std::string>{"dark", "light"});
        check(theme::library().overlays() == std::vector<std::string>{"reduced_motion"});
        const std::vector<std::string> dark{"dark"};
        auto t = theme::library().compose(dark);
        check(t && t->dark && t->color("accent").r == Theme::dark_theme().color("accent").r && t->text_style("display").size == 40);
        Runtime rt;
        check(theme::apply(rt, dark).has_value());
        check_eq(rt.theme().name, std::string("dark"));
        const std::vector<std::string> none{"neon"};
        check(!theme::apply(rt, none).has_value() && rt.theme().name == "dark", "a theme that is not there changes nothing");
    });
    return s.run(argc, argv);
}
