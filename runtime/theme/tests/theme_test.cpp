// Themes as data: the default theme file is the runtime's built-in tokens,
// exactly; overlays change what they name and nothing else; every mistake in
// a file is reported where it is, and every impossible composition says why.
import std;
import openmui.core;
import openmui.view;
import openmui.theme;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

std::filesystem::path repo() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path(); }

std::string text_of(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

bool same_color(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// Every token, compared: what differs, or nothing.
std::string difference(const Theme& a, const Theme& b) {
    if (a.dark != b.dark) return "dark";
    if (a.density != b.density) return "density";
    auto keys = [](const auto& m) {
        std::vector<std::string> k;
        for (const auto& [x, v] : m) k.push_back(x);
        return k;
    };
    if (keys(a.colors) != keys(b.colors)) return "the colours' names";
    for (const auto& [k, v] : a.colors)
        if (!same_color(v, b.colors.at(k))) return "colour " + k;
    if (keys(a.text) != keys(b.text)) return "the text styles' names";
    for (const auto& [k, v] : a.text) {
        const TextToken& w = b.text.at(k);
        if (v.size != w.size || v.weight != w.weight || v.line_height != w.line_height || v.letter_spacing != w.letter_spacing || v.families != w.families)
            return "text style " + k;
    }
    if (a.spacing != b.spacing) return "spacing";
    if (a.radius != b.radius) return "radius";
    if (keys(a.shadows) != keys(b.shadows)) return "the shadows' names";
    for (const auto& [k, v] : a.shadows) {
        const ShadowToken& w = b.shadows.at(k);
        if (v.offset.x != w.offset.x || v.offset.y != w.offset.y || v.blur != w.blur || v.spread != w.spread || !same_color(v.color, w.color))
            return "shadow " + k;
    }
    if (keys(a.motion) != keys(b.motion)) return "the motion tokens' names";
    for (const auto& [k, v] : a.motion) {
        const Transition& w = b.motion.at(k);
        if (v.kind != w.kind || v.duration != w.duration || v.delay != w.delay) return "motion " + k;
    }
    return {};
}

std::string all(const theme::Diagnostics& d) {
    std::string out;
    for (const auto& x : d) out += x.text() + "\n";
    return out;
}

std::vector<std::string> L(std::initializer_list<const char*> names) { return {names.begin(), names.end()}; }

}  // namespace

int main(int argc, char** argv) {
    Suite s{"theme"};

    theme::Library lib;
    const auto default_file = repo() / "themes" / "default" / "default.theme.omui.toml";
    const auto compact_file = repo() / "themes" / "compact" / "compact.theme.omui.toml";

    s.test("the default theme file is the runtime's built-in tokens, light and dark", [&] {
        auto added = lib.add_file(default_file);
        check(added.has_value(), added ? "" : all(added.error()));
        check(lib.themes() == L({"dark", "light"}) && lib.overlays() == L({"reduced_motion"}));
        auto light = lib.compose(L({"light"}));
        check(light.has_value(), light ? "" : light.error());
        if (light) check_eq(difference(*light, Theme::light()), std::string());
        auto dark = lib.compose(L({"dark"}));
        check(dark.has_value(), dark ? "" : dark.error());
        if (dark) {
            check_eq(difference(*dark, Theme::dark_theme()), std::string());
            check_eq(dark->name, std::string("dark"));
        }
    });

    s.test("overlays change what they name and nothing else", [&] {
        check(lib.add_file(compact_file).has_value());
        auto t = lib.compose(L({"dark", "compact", "reduced_motion"}));
        check(t.has_value(), t ? "" : t.error());
        if (!t) return;
        check_eq(t->name, std::string("dark+compact+reduced_motion"));
        check(t->dark && same_color(t->color("background"), Color::hex(0x0b1120)), "the theme's colours");
        check(t->space("s") == 6 && t->corner("medium") == 6, "the density's spacing and radii");
        check(t->density == 0.8f && t->scaled(36) == 29, "controls at 0.8: 36 becomes 29 whole pixels");
        check(t->text_style("body").size == 13 && t->text_style("body").weight == 400 && t->text_style("body").line_height == 1.4f,
              "a text style's fields the overlay does not name stay");
        check(t->transition("motion.normal").duration == anim::seconds(0) && t->transition("motion.spring.snappy").kind == Transition::Kind::tween,
              "reduced motion: every transition at once");
        auto plain = lib.compose(L({"light", "compact"}));
        check(plain && plain->transition("motion.normal").duration > anim::seconds(0), "without the overlay, motion as it was");
    });

    s.test("every mistake in a file, where it is", [] {
        auto r = theme::read(R"t(
[theme.Bright]
doc = "x"
colour = { text = "#000" }

[theme.night]
extends = "light"
[theme.night.colors]
background = "blue"
text = "#123 / 2"
[theme.night.text]
body = { size = 0, wieght = 400 }
caption = 12
[theme.night.motion]
"motion.fast" = "motion.warp"
"motion.slow" = { duration = 300, easing = "wobbly" }

[theme.tight]
overlay = true
extends = "light"
dark = true
)t",
                                 "bad.theme.omui.toml");
        check(!r.has_value());
        if (r) return;
        const std::string text = all(r.error());
        for (const char* expect : {"bad.theme.omui.toml:2:", "theme name `Bright` is not lower_snake_case",
                                   "`colour` is not a key of a theme (`Bright`) (did you mean `colors`?)",
                                   "bad.theme.omui.toml:9:14: colour `background` must be a colour", "colour `text` must be a colour",
                                   "size must be 1 or more, not 0", "`wieght` is not a key of a text style (`body`) (did you mean `weight`?)",
                                   "text style `caption` must be a table", "`motion.warp` is not a motion token",
                                   "easing must be an easing's name or cubic-bezier", "overlay `tight` extends nothing",
                                   "overlay `tight` cannot say whether the theme is dark"})
            check(text.find(expect) != std::string::npos, std::format("{}\n--- in ---\n{}", expect, text));
        auto syntax = theme::read("[theme.x\ncolors = 1", "broken.theme.omui.toml");
        check(!syntax && syntax.error().size() == 1 && syntax.error()[0].line == 1, syntax ? "" : all(syntax.error()));
    });

    s.test("what cannot be composed, and why", [&] {
        auto none = lib.compose(L({"sepia"}));
        check(!none && none.error().find("no theme is named `sepia` (themes: dark, light)") != std::string::npos, none ? "" : none.error());
        auto overlay_first = lib.compose(L({"compact"}));
        check(!overlay_first && overlay_first.error().find("`compact` is an overlay") != std::string::npos);
        auto theme_as_overlay = lib.compose(L({"light", "dark"}));
        check(!theme_as_overlay && theme_as_overlay.error().find("`dark` is a theme, not an overlay") != std::string::npos);
        theme::Library more;
        check(more.add(R"t(
[theme.base]
[theme.base.colors]
background = "#fff"
[theme.typo]
extends = "base"
[theme.typo.colors]
backgruond = "#000"
[theme.orphan]
extends = "nowhere"
[theme.a]
extends = "b"
[theme.b]
extends = "a"
)t", "more.theme.omui.toml").has_value());
        auto typo = more.compose(L({"typo"}));
        check(!typo && typo.error().find("`typo` sets the colour `backgruond`, which the theme under it does not have (did you mean `background`?)") !=
                           std::string::npos,
              typo ? "" : typo.error());
        auto orphan = more.compose(L({"orphan"}));
        check(!orphan && orphan.error().find("extends `nowhere`, which no theme file declares") != std::string::npos);
        auto cycle = more.compose(L({"a"}));
        check(!cycle && cycle.error().find("extends itself") != std::string::npos, cycle ? "" : cycle.error());
    });

    s.test("a file added again replaces what it declared; another file may not take a name", [&] {
        theme::Library l;
        check(l.add("[theme.one]\n[theme.one.colors]\ntext = \"#000\"\n", "a.theme.omui.toml").has_value());
        check(l.add("[theme.one]\n[theme.one.colors]\ntext = \"#fff\"\n", "a.theme.omui.toml").has_value(), "a saved edit");
        auto one = l.compose(L({"one"}));
        check(one && same_color(one->color("text"), Color::hex(0xffffff)));
        auto clash = l.add("[theme.one]\n", "b.theme.omui.toml");
        check(!clash && all(clash.error()).find("`one` is already declared by a.theme.omui.toml") != std::string::npos);
        auto bad = l.add("[theme.one]\n[theme.one.colors]\ntext = \"nope\"\n", "a.theme.omui.toml");
        check(!bad, "a broken edit is refused");
        auto still = l.compose(L({"one"}));
        check(still && same_color(still->color("text"), Color::hex(0xffffff)), "and changes nothing");
    });

    return s.run(argc, argv);
}
