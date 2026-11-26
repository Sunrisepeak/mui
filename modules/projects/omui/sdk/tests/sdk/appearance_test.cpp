// AppSpec::dark_theme: an application with a dark theme of its own follows the
// person's preference for a light or a dark appearance, as the host reads it;
// `--dark` shows its dark theme outright.
import std;
import omui;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {

Theme night() {
    Theme theme = Theme::dark_theme();
    theme.name = "night";
    return theme;
}

}  // namespace

static UTest cases{"sdk.appearance", [] {
    const auto headless = [] {
        RunOptions options;
        options.headless = true;
        options.frames = 1;
        options.level = 0;
        return options;
    };

    "an application with a dark theme of its own follows the person's preference"_test = [&] {
        AppSpec app;
        app.dark_theme = night();
        std::vector<std::string> seen;
        app.setup = [&](Runtime& rt) {
            seen.push_back(std::format("{} follows={}", rt.theme().name, rt.theme_follows_preference()));
            rt.set_dark_preferred(true);  // as a host does when the system turns dark
            seen.push_back(rt.theme().name);
            rt.set_dark_preferred(false);
            seen.push_back(rt.theme().name);
        };
        expect(eq(run(std::move(app), headless()), 0));
        expect(seen == std::vector<std::string>{std::format("{} follows=true", Theme::light().name), "night", Theme::light().name}) << std::format("{}", seen);
    };

    "an application without one keeps its theme whatever is preferred"_test = [&] {
        AppSpec app;
        bool follows = true, dark = true;
        app.setup = [&](Runtime& rt) {
            follows = rt.theme_follows_preference();
            rt.set_dark_preferred(true);
            dark = rt.theme().dark;
        };
        expect(eq(run(std::move(app), headless()), 0));
        expect(!follows && !dark);
    };

    "--dark: its dark theme outright, the default one where it has none"_test = [&] {
        for (const bool own : {true, false}) {
            AppSpec app;
            if (own) app.dark_theme = night();
            RunOptions options = headless();
            options.dark = true;
            std::string name;
            bool follows = true, dark = false;
            app.setup = [&](Runtime& rt) {
                follows = rt.theme_follows_preference();
                rt.set_dark_preferred(false);
                name = rt.theme().name;
                dark = rt.theme().dark;
            };
            expect(eq(run(std::move(app), options), 0));
            expect(!follows && dark && name == (own ? std::string("night") : Theme::dark_theme().name)) << name;
        }
    };
}};
