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

static UTest cases{"theme.default", [] {
    "the embedded file: light, dark and reduced motion, the built-in tokens"_test = [] {
        auto added = themes::add_default();
        expect(added.has_value()) << (added ? "" : added.error()[0].text());
        expect(themes::add_default().has_value()) << "again: the same file, the same themes";
        expect(theme::library().themes() == std::vector<std::string>{"dark", "light"});
        expect(theme::library().overlays() == std::vector<std::string>{"reduced_motion"});
        expect(theme::library().schemes() == std::vector<std::string>{"system"});
        const std::vector<std::string> dark{"dark"};
        auto t = theme::library().compose(dark);
        expect(t && t->dark && t->color("accent").r == Theme::dark_theme().color("accent").r && t->text_style("display").size == 40);
        Runtime rt;
        expect(theme::apply(rt, dark).has_value());
        expect(eq(rt.theme().name, std::string("dark")));
        const std::vector<std::string> none{"neon"};
        expect(!theme::apply(rt, none).has_value() && rt.theme().name == "dark") << "a theme that is not there changes nothing";
    };

    "the system scheme: the runtime's theme follows the person's preference, until a theme is named outright"_test = [] {
        expect(themes::add_default().has_value());
        Runtime rt;
        const std::vector<std::string> system{"system"}, light{"light"};
        expect(!rt.theme_follows_preference());
        expect(theme::apply(rt, system).has_value());
        expect(rt.theme_follows_preference() && !rt.theme().dark && rt.theme().name == "system") << rt.theme().name;
        rt.set_dark_preferred(true);
        expect(rt.theme().dark && rt.theme().name == "system" && rt.theme().color("background").r == Theme::dark_theme().color("background").r) << "the person prefers dark: dark";
        rt.set_dark_preferred(false);
        expect(!rt.theme().dark) << "and light again";
        // A runtime started where dark is preferred starts dark.
        RuntimeOptions preferred;
        preferred.dark_preferred = true;
        Runtime night(preferred);
        expect(theme::apply(night, system).has_value() && night.theme().dark);
        // Named outright, the theme stays whatever is preferred.
        expect(theme::apply(rt, light).has_value() && !rt.theme_follows_preference());
        rt.set_dark_preferred(true);
        expect(!rt.theme().dark && rt.theme().name == "light");
    };
}};
