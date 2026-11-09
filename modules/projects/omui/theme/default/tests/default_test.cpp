// The default theme as a program carries it: add_default() puts light, dark
// and reduced_motion in the program's library, light and dark are the
// runtime's built-in tokens, and adding again changes nothing.
import std;
import omui.core;
import omui.view;
import omui.theme;
import omui.theme.default_theme;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;

UTest cases{"theme.default", [] {
    "the embedded file: light, dark and reduced motion, the built-in tokens"_test = [] {
        auto added = themes::add_default();
        expect(added.has_value()) << (added ? "" : added.error()[0].text());
        expect(themes::add_default().has_value()) << "again: the same file, the same themes";
        expect(theme::library().themes() == std::vector<std::string>{"dark", "light"});
        expect(theme::library().overlays() == std::vector<std::string>{"reduced_motion"});
        const std::vector<std::string> dark{"dark"};
        auto t = theme::library().compose(dark);
        expect(t && t->dark && t->color("accent").r == Theme::dark_theme().color("accent").r && t->text_style("display").size == 40);
        Runtime rt;
        expect(theme::apply(rt, dark).has_value());
        expect(eq(rt.theme().name, std::string("dark")));
        const std::vector<std::string> none{"neon"};
        expect(!theme::apply(rt, none).has_value() && rt.theme().name == "dark") << "a theme that is not there changes nothing";
    };
}};
