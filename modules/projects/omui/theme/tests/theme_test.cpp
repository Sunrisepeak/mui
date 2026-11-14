// Themes as data: the default theme file is the runtime's built-in tokens,
// exactly; overlays change what they name and nothing else; every mistake in
// a file is reported where it is, and every impossible composition says why.
import std;
import omui.core;
import omui.view;
import omui.theme;
import xrender.vector;
import omui.lang.ts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

// The theme package, whose packs (default, compact) lie inside it.
std::filesystem::path theme_dir() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

// A theme module as the JSON the library reads: what it evaluates to.
std::string snapshot_of(const std::filesystem::path& file) {
    const auto data = lang::ts::evaluate(file);
    return data ? std::format("{{\"{}\":{}}}", data->kind, data->json) : data.error();
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

UTest cases{"theme", [] {

    theme::Library lib;
    "effect animation and appearance rules roundtrip with strict channel diagnostics"_test = [] {
        const std::string source = R"({"style": {"pixel_probe": {"theme": "light", "effects": {"painter": {"effect": "pixelate", "size": 8, "usage": "appearance", "duration": 0.5, "reverse": true, "animate": {"amount": [0.0, 1.0], "origin_x": [-2.0, 3.0]}}}}}})";
        const auto read = theme::read_styles(source, "pixel-probe.style.omui.json");
        expect(read.has_value()) << (read ? "" : all(read.error())); if (!read) return;
        const auto written = theme::write_style(read->front());
        const auto again = theme::read_styles(written, "roundtrip.style.omui.json");
        expect(again.has_value()) << (again ? "" : all(again.error())); if (!again) return;
        expect(again->front().effects.front().effect == read->front().effects.front().effect);
        for (const auto rule : {R"("animate": {"missing": [0, 1]}, "duration": 1)", R"("animate": {"amount": [0, 2]}, "duration": 1)",
            R"("animate": {"amount": [0]}, "duration": 1)", R"("animate": {"amount": [0, 1]})", R"("animate": {"amount": [0, 1]}, "duration": -1)",
            R"("animate": {"amount": [0, 1]}, "duration": null)", R"("usage": "missing")", R"("reverse": 1)"}) {
            const auto bad = theme::read_styles(std::format(R"({{"style": {{"x": {{"theme": "light", "effects": {{"painter": {{"effect": "pixelate", {}}}}}}}}}}})", rule), "bad.style.omui.json");
            expect(!bad) << rule;
        }
    };
    const auto default_snapshot = snapshot_of(theme_dir() / "default" / "default.theme.omui.ts");
    const auto compact_snapshot = snapshot_of(theme_dir() / "compact" / "compact.theme.omui.ts");

    "the default theme file is the runtime's built-in tokens, light and dark"_test = [&] {
        auto added = lib.add(default_snapshot, "default.theme.omui.json");
        expect(added.has_value()) << (added ? "" : all(added.error()));
        expect(lib.themes() == L({"dark", "light"}) && lib.overlays() == L({"reduced_motion"}));
        auto light = lib.compose(L({"light"}));
        expect(light.has_value()) << (light ? "" : light.error());
        if (light) expect(eq(difference(*light, Theme::light()), std::string()));
        auto dark = lib.compose(L({"dark"}));
        expect(dark.has_value()) << (dark ? "" : dark.error());
        if (dark) {
            expect(eq(difference(*dark, Theme::dark_theme()), std::string()));
            expect(eq(dark->name, std::string("dark")));
        }
    };

    "overlays change what they name and nothing else"_test = [&] {
        expect(lib.add(compact_snapshot, "compact.theme.omui.json").has_value());
        auto t = lib.compose(L({"dark", "compact", "reduced_motion"}));
        expect(t.has_value()) << (t ? "" : t.error());
        if (!t) return;
        expect(eq(t->name, std::string("dark+compact+reduced_motion")));
        expect(t->dark && same_color(t->color("background"), Color::hex(0x0b1120))) << "the theme's colours";
        expect(t->space("s") == 6 && t->corner("medium") == 6) << "the density's spacing and radii";
        expect(t->density == 0.8f && t->scaled(36) == 29) << "controls at 0.8: 36 becomes 29 whole pixels";
        expect(t->text_style("body").size == 13 && t->text_style("body").weight == 400 && t->text_style("body").line_height == 1.4f) << "a text style's fields the overlay does not name stay";
        expect(t->transition("motion.normal").duration == hanim::seconds(0) && t->transition("motion.spring.snappy").kind == Transition::Kind::tween) << "reduced motion: every transition at once";
        auto plain = lib.compose(L({"light", "compact"}));
        expect(plain && plain->transition("motion.normal").duration > hanim::seconds(0)) << "without the overlay, motion as it was";
    };

    "every mistake in a file, where it is"_test = [] {
        auto r = theme::read(R"t({"theme": {
  "Bright": {"doc": "x", "colour": {"text": "#000"}},
  "night": {
    "extends": "light",
    "colors": {"background": "blue", "text": "#123 / 2"},
    "text": {"body": {"size": 0, "wieght": 400}, "caption": 12},
    "motion": {"motion.fast": "motion.warp", "motion.slow": {"duration": 300, "easing": "wobbly"}}
  },
  "tight": {"overlay": true, "extends": "light", "dark": true}
}})t",
                             "bad.theme.omui.json");
        expect(!r.has_value());
        if (r) return;
        const std::string text = all(r.error());
        for (const char* wanted : {"bad.theme.omui.json:2:13: /theme/Bright: theme name `Bright` is not lower_snake_case",
                                   "/theme/Bright/colour: `colour` is not a key of a theme (`Bright`) (did you mean `colors`?)",
                                   "bad.theme.omui.json:5:30: /theme/night/colors/background: colour `background` must be a colour", "colour `text` must be a colour",
                                   "size must be 1 or more, not 0", "`wieght` is not a key of a text style (`body`) (did you mean `weight`?)",
                                   "/theme/night/text/caption: text style `caption` must be an object", "`motion.warp` is not a motion token",
                                   "easing must be an easing's name or cubic-bezier", "overlay `tight` extends nothing",
                                   "overlay `tight` cannot say whether the theme is dark"})
            expect(text.find(wanted) != std::string::npos) << std::format("{}\n--- in ---\n{}", wanted, text);
        auto syntax = theme::read("{\"theme\": {\"x\"", "broken.theme.omui.json");
        expect(!syntax && syntax.error().size() == 1 && syntax.error()[0].line == 1) << (syntax ? "" : all(syntax.error()));
    };

    "what cannot be composed, and why"_test = [&] {
        auto none = lib.compose(L({"sepia"}));
        expect(!none && none.error().find("no theme is named `sepia` (themes: dark, light)") != std::string::npos) << (none ? "" : none.error());
        auto overlay_first = lib.compose(L({"compact"}));
        expect(!overlay_first && overlay_first.error().find("`compact` is an overlay") != std::string::npos);
        auto theme_as_overlay = lib.compose(L({"light", "dark"}));
        expect(!theme_as_overlay && theme_as_overlay.error().find("`dark` is a theme, not an overlay") != std::string::npos);
        theme::Library more;
        expect(more.add(R"t({"theme": {
  "base": {"colors": {"background": "#fff"}},
  "typo": {"extends": "base", "colors": {"backgruond": "#000"}},
  "orphan": {"extends": "nowhere"},
  "a": {"extends": "b"},
  "b": {"extends": "a"}
}})t", "more.theme.omui.json").has_value());
        auto typo = more.compose(L({"typo"}));
        expect(!typo && typo.error().find("`typo` sets the colour `backgruond`, which the theme under it does not have (did you mean `background`?)") !=
                           std::string::npos) << (typo ? "" : typo.error());
        auto orphan = more.compose(L({"orphan"}));
        expect(!orphan && orphan.error().find("extends `nowhere`, which no theme file declares") != std::string::npos);
        auto cycle = more.compose(L({"a"}));
        expect(!cycle && cycle.error().find("extends itself") != std::string::npos) << (cycle ? "" : cycle.error());
    };

    "a file added again replaces what it declared; another file may not take a name"_test = [&] {
        theme::Library l;
        expect(l.add(R"({"theme": {"one": {"colors": {"text": "#000"}}}})", "a.theme.omui.json").has_value());
        expect(l.add(R"({"theme": {"one": {"colors": {"text": "#fff"}}}})", "a.theme.omui.json").has_value()) << "a saved edit";
        auto one = l.compose(L({"one"}));
        expect(one && same_color(one->color("text"), Color::hex(0xffffff)));
        auto clash = l.add(R"({"theme": {"one": {}}})", "b.theme.omui.json");
        expect(!clash && all(clash.error()).find("`one` is already declared by a.theme.omui.json") != std::string::npos);
        auto bad = l.add(R"({"theme": {"one": {"colors": {"text": "nope"}}}})", "a.theme.omui.json");
        expect(!bad) << "a broken edit is refused";
        auto still = l.compose(L({"one"}));
        expect(still && same_color(still->color("text"), Color::hex(0xffffff))) << "and changes nothing";
    };

    "style packs (S2, D-69): tokens over a theme, effects on the window, a type and a role; the semantic tree the same"_test = [&] {
        const char* neon = R"json({"style": {"neon": {
  "doc": "Light in the dark.",
  "theme": "dark",
  "crossfade": 0.4,
  "colors": {"accent": "#22d3ee"},
  "effects": {
    "window": {"effect": "crt", "curvature": 0.06, "scanlines": 0.4},
    "button": {"effect": "neon", "radius": 10, "intensity": 1.2},
    "role:group": {"effect": "metaball", "smoothness": 12},
    "text": {"effect": "paper", "grain": 0.2}
  }
}}})json";
        auto added = lib.add_styles(neon, "neon.style.omui.json");
        expect(added.has_value()) << (added ? "" : all(added.error()));
        expect(lib.styles() == L({"neon"}));
        const auto st = lib.find_style("neon");
        expect(st && st->layers == L({"dark"}) && st->effects.size() == 4 && st->crossfade == 0.4);
        auto t = lib.compose_style("neon");
        expect(t.has_value()) << (t ? "" : t.error());
        if (t) {
            expect(t->dark) << "over the dark theme";
            expect(same_color(t->color("accent"), Color::hex(0x22d3ee))) << "its accent";
            expect(eq(t->name, std::string("dark@neon")));
        }
        // On a runtime: the rules reach the window, buttons and groups; nothing else changes.
        Runtime rt({.size = {200, 120}});
        auto view = [] {
            return column({.id = "ui", .padding = 10, .gap = 8},
                          {box({.id = "cta", .width = 80, .height = 24, .background = "accent", .radius = 12, .on = {{"press", [](const Value&) {}}}}),
                           text({.text = "Hello"})});
        };
        rt.mount("main", view());
        rt.frame();
        const auto before = rt.display_list().commands().size();
        std::vector<spec::SemanticNode> tree_before;
        for (NodeId id : {rt.find("main"), rt.find("main/cta")}) tree_before.push_back(rt.semantic_node(id));
        // The library here is the test's: apply through the program's library.
        expect(theme::library().add(default_snapshot, "default.theme.omui.json").has_value() || true);
        expect(theme::library().add_styles(neon, "neon.style.omui.json").has_value());
        auto applied = theme::apply_style(rt, "neon");
        expect(applied.has_value()) << (applied ? "" : applied.error());
        rt.frame();
        // Layers with effects: the window's crt at least, and the cross-fade's layer.
        std::size_t effect_layers = 0, plain_layers = 0;
        for (const auto& c : rt.display_list().commands())
            if (const auto* l = std::get_if<xrender::PushLayer>(&c)) (l->effect.id != 0 ? effect_layers : plain_layers) += 1;
        expect(effect_layers >= 2) << std::format("the window's effect and the text's: {} effect layers", effect_layers);
        expect(plain_layers >= 1) << "the cross-fade from the frame before";
        expect(rt.display_list().commands().size() > before) << "the old frame drawn over, fading";
        for (std::size_t i = 0; i < tree_before.size(); ++i) {
            const NodeId id = i == 0 ? rt.find("main") : rt.find("main/cta");
            const auto now = rt.semantic_node(id);
            expect(now.role == tree_before[i].role && now.name == tree_before[i].name && now.actions == tree_before[i].actions && now.states == tree_before[i].states) << "the same semantic node under the style";
        }
        // After the cross-fade, only the style's layers.
        rt.step_time(seconds(1));
        rt.frame();
        rt.frame();
        std::size_t plain_after = 0;
        for (const auto& c : rt.display_list().commands())
            if (const auto* l = std::get_if<xrender::PushLayer>(&c); l && l->effect.id == 0) ++plain_after;
        expect(eq(plain_after, std::size_t{0}));
        // A style's mistakes, where they are, with the names meant.
        const char* wrong = R"json(
{"style": {"bad": {
  "theme": "dark",
  "effects": {
    "window": {"effect": "crtt"},
    "button": {"effect": "neon", "radios": 3},
    "card": {"radius": 3}
  }
}}})json";
        auto bad = theme::read_styles(wrong, "bad.style.omui.json");
        expect(!bad.has_value()) << "refused";
        if (!bad) {
            const std::string text = all(bad.error());
            expect(text.contains("bad.style.omui.json:5:") && text.contains("/style/bad/effects/window/effect: no effect is named `crtt`") && text.contains("did you mean `crt`?")) << text;
            expect(text.contains("bad.style.omui.json:6:") && text.contains("/style/bad/effects/button/radios:") && text.contains("did you mean `radius`?")) << text;
            expect(text.contains("bad.style.omui.json:7:") && text.contains("names no `effect`")) << text;
        }
        auto themeless = theme::read_styles(R"({"style": {"x": {"doc": "no theme"}}})", "x.style.omui.json");
        expect(!themeless.has_value() && all(themeless.error()).contains("names no theme")) << "a style starts from a theme";
        expect(!lib.compose_style("nope").has_value()) << "an unknown style";
        // Written out and read back: the same style.
        const std::string text = theme::write_style(*st);
        auto again = theme::read_styles(text, "again.style.omui.json");
        expect(again.has_value()) << (again ? text : all(again.error()) + "\n" + text);
        if (again) {
            const theme::Style& b = again->front();
            expect(b.name == st->name && b.doc == st->doc && b.layers == st->layers && b.crossfade == st->crossfade) << text;
            expect(b.tokens.colors.size() == st->tokens.colors.size() && same_color(b.tokens.colors.at("accent"), st->tokens.colors.at("accent"))) << text;
            expect(b.effects.size() == st->effects.size()) << text;
            for (std::size_t i = 0; i < b.effects.size() && i < st->effects.size(); ++i)
                expect(b.effects[i].selector == st->effects[i].selector && b.effects[i].effect.json() == st->effects[i].effect.json()) << std::format("{} vs {}", b.effects[i].effect.json(), st->effects[i].effect.json());
            expect(eq(theme::write_style(b), text));
        }
    };

    "a style's corners and shadows: continuous curvature and an ambient layer, carried into the theme and written back; out of range refused"_test = [&] {
        const char* smooth = R"json({"style": {"smooth": {
  "theme": "light",
  "corner_smoothing": 0.6,
  "shadows": {
    "lg": {"x": 0, "y": 12, "blur": 24, "spread": -6, "color": "#0f172a2e", "ambient": {"x": 0, "y": 2, "blur": 48, "spread": 0, "color": "#0f172a14"}},
    "control_accent": {"x": 0, "y": 2, "blur": 4, "spread": -1, "color": "#0f172a38", "tint": 0.6}
  }
}}})json";
        auto added = lib.add_styles(smooth, "smooth.style.omui.json");
        expect(added.has_value()) << (added ? "" : all(added.error()));
        auto t = lib.compose_style("smooth");
        expect(t.has_value() && t->corner_smoothing == 0.6f && t->radii(12) == CornerRadii::all(12).smoothed(0.6f)) << "the theme's corners are smoothed";
        if (const auto st = lib.find_style("smooth")) {
            const std::string text = theme::write_style(*st);
            expect(text.contains(R"("corner_smoothing": 0.6)") && text.contains(R"("ambient": { "x": 0, "y": 2, "blur": 48)") && text.contains(R"("tint": 0.6)")) << text;
        }
        // Shadows in layers: the key light's, and under it the ambient one.
        if (t) {
            const auto lg = t->shadows.find("lg");
            expect(lg != t->shadows.end() && lg->second.blur == 24 && lg->second.ambient && lg->second.ambient->blur == 48) << "the lg shadow has its ambient layer";
        }
        // A shadow that takes the colour of what casts it.
        if (t) {
            const auto accent = t->shadows.find("control_accent");
            expect(accent != t->shadows.end() && accent->second.tint == 0.6f) << "the accent shadow is tinted";
        }
        auto bad = lib.add_styles(R"({"style": {"rough": {"theme": "light", "corner_smoothing": 2}}})", "rough.style.omui.json");
        expect(!bad.has_value()) << "2 is out of range";
        auto overtinted = lib.add_styles(R"({"style": {"ink2": {"theme": "light", "shadows": {"sm": {"tint": 1.5}}}}})", "ink2.style.omui.json");
        expect(!overtinted.has_value()) << "a tint over 1 is refused";
    };

    "the defaults' finish (B4): continuous corners, shadows in two layers, controls' depth by state; dark tuned on its own"_test = [&] {
        auto t = lib.compose(L({"light"}));
        expect(t.has_value()) << (t ? "" : t.error());
        if (!t) return;
        expect(t->corner_smoothing == 0.6f);
        for (const char* name : {"sm", "md", "lg", "control", "control:hovered", "control:pressed", "control_accent", "thumb", "thumb:pressed", "switch_thumb"}) {
            const auto it = t->shadows.find(name);
            expect(it != t->shadows.end() && it->second.ambient.has_value()) << name;
        }
        expect(t->shadows.at("control:hovered").offset.y > t->shadows.at("control").offset.y &&
                  t->shadows.at("control:pressed").offset.y < t->shadows.at("control").offset.y) << "lifted under the pointer, tighter pressed";
        expect(t->shadows.at("control_accent").tint > 0) << "an accent control's shadow takes its colour";
        auto dark = lib.compose(L({"dark"}));
        expect(dark && dark->shadows.at("md").color.a > t->shadows.at("md").color.a && dark->shadows.at("md").color.r == 0) << "dark: black and stronger, not the light theme's";
    };

    "a style's motions (universal motion §5.2): over the framework's rules, a slot's for every node they move, a role's own; candidates; mistakes refused"_test = [&] {
        const char* ink = R"json({"style": {"inky": {
  "theme": "light",
  "motions": {
    "update": ["ink", "simple"],
    "state": "simple",
    "role:dialog": {"enter": {"use": "spring", "appear": {"presence": "rise_in", "distance": 12, "also": "settle_in"}}}
  }
}}})json";
        auto added = lib.add_styles(ink, "inky.style.omui.json");
        expect(added.has_value()) << (added ? "" : all(added.error()));
        const auto st = lib.find_style("inky");
        expect(st && st->motions.size() == 3) << "three motions read";
        auto t = lib.compose_style("inky");
        expect(t.has_value()) << (t ? "" : t.error());
        if (t) {
            const auto update = std::ranges::find_if(t->motion_rules, [](const MotionRule& r) { return r.layer == MotionRule::Layer::style && r.target == "*" && r.slot == "update"; });
            expect(update != t->motion_rules.end() && update->spec.name == "ink" && update->instead.size() == 1 && update->instead[0].name == "simple") << "the style applies ink to every node, with a simple fallback";
            const auto state = std::ranges::find_if(t->motion_rules, [](const MotionRule& r) { return r.layer == MotionRule::Layer::style && r.target == "*" && r.slot == "state"; });
            expect(state != t->motion_rules.end() && state->spec.name == "simple") << "state feedback is the style's own slot";
            expect(std::ranges::any_of(t->motion_rules, [](const MotionRule& r) { return r.layer == MotionRule::Layer::framework && r.target == "checkbox" && r.spec.name == "default.check"; })) << "typed defaults remain available under the style layer";
            const auto dialog = std::ranges::find_if(t->motion_rules, [](const MotionRule& r) { return r.target == "role:dialog" && r.slot == "enter"; });
            expect(dialog != t->motion_rules.end() && dialog->spec.name == "spring" && dialog->spec.appear.away.offset.y == 12 && dialog->spec.appear.away.scale.x == .96 &&
                      dialog->spec.appear.name == "rise_in+settle_in" && dialog->layer == MotionRule::Layer::style_targeted) << (dialog == t->motion_rules.end() ? std::string("the dialog's enter: none") : std::format("the dialog's enter: {} {}", dialog->spec.name, dialog->spec.appear.name));
        }
        // Written out and read back: the same motions.
        if (st) {
            const std::string text = theme::write_style(*st);
            auto again = theme::read_styles(text, "again.style.omui.json");
            expect(again && again->front().motions.size() == 3 && theme::write_style(again->front()) == text) << text;
        }
        auto wrong = theme::read_styles(R"({"style": {"w": {"theme": "light", "motions": {"updtae": "ink", "button": {"later": "ink"}}}}})", "w.style.omui.json");
        expect(!wrong.has_value() && all(wrong.error()).contains("`updtae` is not a slot") && all(wrong.error()).contains("`later` is not a slot")) << (wrong ? "accepted" : all(wrong.error()));
        expect(lib.add_styles(R"({"style": {"v": {"theme": "light", "motions": {"update": "swirl"}}}})", "v.style.omui.json").has_value());
        auto unknown = lib.compose_style("v");
        expect(!unknown.has_value() && unknown.error().contains("`\"swirl\"` is not a motion")) << (unknown ? "composed" : unknown.error());
    };
}};
