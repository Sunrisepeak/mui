// The compact density as a program carries it: an overlay over whichever
// theme it is composed with.
import std;
import omui.core;
import omui.view;
import omui.theme;
import omui.theme.compact;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;

UTest cases{"theme.compact", [] {
    "an overlay: smaller spacing, text and radii, over any theme"_test = [] {
        expect(themes::add_compact().has_value());
        expect(theme::library().overlays() == std::vector<std::string>{"compact"});
        auto base = theme::library().add(R"({"theme": {"plain": {"spacing": {"s": 8}, "text": {"body": {"size": 15}}, "radius": {"medium": 8}}}})",
                                         "plain.theme.omui.json");
        expect(base.has_value());
        const std::vector<std::string> layers{"plain", "compact"};
        auto t = theme::library().compose(layers);
        expect(!t && t.error().find("`compact` sets the spacing `l`, which the theme under it does not have") != std::string::npos &&
                  t.error().find("did you mean") == std::string::npos) << (t ? "composed" : t.error());
    };
}};
