// Themes as data: the default theme file is the runtime's built-in tokens,
// exactly; overlays change what they name and nothing else; every mistake in
// a file is reported where it is, and every impossible composition says why.
import std;
import omui.core;
import omui.view;
import omui.theme;
import xrender.vector;
import mlibs.testing;

using namespace mlibs::testing;
using namespace omui;

namespace {

// The theme package, whose packs (default, compact) lie inside it.
std::filesystem::path theme_dir() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

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
    s.test("effect animation and appearance rules roundtrip with strict channel diagnostics", [] {
        const std::string source = "[style.pixel_probe]\ntheme = \"light\"\n[style.pixel_probe.effects]\npainter = { effect = \"pixelate\", size = 8, usage = \"appearance\", duration = 0.5, reverse = true, animate = { amount = [0.0, 1.0], origin_x = [-2.0, 3.0] } }\n";
        const auto read = theme::read_styles(source, "pixel-probe.style.omui.toml");
        check(read.has_value(), read ? "" : all(read.error())); if (!read) return;
        const auto written = theme::write_style(read->front());
        const auto again = theme::read_styles(written, "roundtrip.style.omui.toml");
        check(again.has_value(), again ? "" : all(again.error())); if (!again) return;
        check(again->front().effects.front().effect == read->front().effects.front().effect);
        for (const auto rule : {"animate = { missing = [0, 1] }, duration = 1", "animate = { amount = [0, 2] }, duration = 1",
            "animate = { amount = [0] }, duration = 1", "animate = { amount = [0, 1] }", "animate = { amount = [0, 1] }, duration = -1",
            "animate = { amount = [0, 1] }, duration = nan", "usage = \"missing\"", "reverse = 1"}) {
            const auto bad = theme::read_styles(std::format("[style.x]\ntheme = \"light\"\n[style.x.effects]\npainter = {{ effect = \"pixelate\", {} }}\n", rule), "bad.style.omui.toml");
            check(!bad, rule);
        }
    });
    const auto default_file = theme_dir() / "default" / "default.theme.omui.toml";
    const auto compact_file = theme_dir() / "compact" / "compact.theme.omui.toml";

    s.test("the default theme file is the runtime's built-in tokens, light and dark", [&] {
        auto added = lib.add_file(default_file);
        check(added.has_value(), added ? "" : all(added.error()));
        // `refined` is staged (B4 second pass: the state's halos) until the maintainer approves it.
        check(lib.themes() == L({"dark", "light"}) && lib.overlays() == L({"reduced_motion", "refined"}));
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
        check(t->transition("motion.normal").duration == hanim::seconds(0) && t->transition("motion.spring.snappy").kind == Transition::Kind::tween,
              "reduced motion: every transition at once");
        auto plain = lib.compose(L({"light", "compact"}));
        check(plain && plain->transition("motion.normal").duration > hanim::seconds(0), "without the overlay, motion as it was");
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

    s.test("style packs (S2, D-69): tokens over a theme, effects on the window, a type and a role; the semantic tree the same", [&] {
        const char* neon = R"toml(
[style.neon]
doc       = "Light in the dark."
theme     = "dark"
crossfade = 0.4
[style.neon.colors]
accent = "#22d3ee"
[style.neon.effects]
window       = { effect = "crt", curvature = 0.06, scanlines = 0.4 }
button       = { effect = "neon", radius = 10, intensity = 1.2 }
"role:group" = { effect = "metaball", smoothness = 12 }
text         = { effect = "paper", grain = 0.2 }
)toml";
        auto added = lib.add_styles(neon, "neon.style.omui.toml");
        check(added.has_value(), added ? "" : all(added.error()));
        check(lib.styles() == L({"neon"}));
        const auto st = lib.find_style("neon");
        check(st && st->layers == L({"dark"}) && st->effects.size() == 4 && st->crossfade == 0.4);
        auto t = lib.compose_style("neon");
        check(t.has_value(), t ? "" : t.error());
        if (t) {
            check(t->dark, "over the dark theme");
            check(same_color(t->color("accent"), Color::hex(0x22d3ee)), "its accent");
            check_eq(t->name, std::string("dark@neon"));
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
        check(theme::library().add_file(default_file).has_value() || true);
        check(theme::library().add_styles(neon, "neon.style.omui.toml").has_value());
        auto applied = theme::apply_style(rt, "neon");
        check(applied.has_value(), applied ? "" : applied.error());
        rt.frame();
        // Layers with effects: the window's crt at least, and the cross-fade's layer.
        std::size_t effect_layers = 0, plain_layers = 0;
        for (const auto& c : rt.display_list().commands())
            if (const auto* l = std::get_if<xrender::PushLayer>(&c)) (l->effect.id != 0 ? effect_layers : plain_layers) += 1;
        check(effect_layers >= 2, std::format("the window's effect and the text's: {} effect layers", effect_layers));
        check(plain_layers >= 1, "the cross-fade from the frame before");
        check(rt.display_list().commands().size() > before, "the old frame drawn over, fading");
        for (std::size_t i = 0; i < tree_before.size(); ++i) {
            const NodeId id = i == 0 ? rt.find("main") : rt.find("main/cta");
            const auto now = rt.semantic_node(id);
            check(now.role == tree_before[i].role && now.name == tree_before[i].name && now.actions == tree_before[i].actions && now.states == tree_before[i].states,
                  "the same semantic node under the style");
        }
        // After the cross-fade, only the style's layers.
        rt.step_time(seconds(1));
        rt.frame();
        rt.frame();
        std::size_t plain_after = 0;
        for (const auto& c : rt.display_list().commands())
            if (const auto* l = std::get_if<xrender::PushLayer>(&c); l && l->effect.id == 0) ++plain_after;
        check_eq(plain_after, std::size_t{0});
        // A style's mistakes, where they are, with the names meant.
        const char* wrong = R"toml(
[style.bad]
theme = "dark"
[style.bad.effects]
window = { effect = "crtt" }
button = { effect = "neon", radios = 3 }
card   = { radius = 3 }
)toml";
        auto bad = theme::read_styles(wrong, "bad.style.omui.toml");
        check(!bad.has_value(), "refused");
        if (!bad) {
            const std::string text = all(bad.error());
            check(text.contains("bad.style.omui.toml:5") && text.contains("did you mean `crt`?"), text);
            check(text.contains("bad.style.omui.toml:6") && text.contains("did you mean `radius`?"), text);
            check(text.contains("bad.style.omui.toml:7") && text.contains("names no `effect`"), text);
        }
        auto themeless = theme::read_styles("[style.x]\ndoc = \"no theme\"\n", "x.style.omui.toml");
        check(!themeless.has_value() && all(themeless.error()).contains("names no theme"), "a style starts from a theme");
        check(!lib.compose_style("nope").has_value(), "an unknown style");
        // Written out and read back: the same style.
        const std::string text = theme::write_style(*st);
        auto again = theme::read_styles(text, "again.style.omui.toml");
        check(again.has_value(), again ? text : all(again.error()) + "\n" + text);
        if (again) {
            const theme::Style& b = again->front();
            check(b.name == st->name && b.doc == st->doc && b.layers == st->layers && b.crossfade == st->crossfade, text);
            check(b.tokens.colors.size() == st->tokens.colors.size() && same_color(b.tokens.colors.at("accent"), st->tokens.colors.at("accent")), text);
            check(b.effects.size() == st->effects.size(), text);
            for (std::size_t i = 0; i < b.effects.size() && i < st->effects.size(); ++i)
                check(b.effects[i].selector == st->effects[i].selector && b.effects[i].effect.json() == st->effects[i].effect.json(),
                      std::format("{} vs {}", b.effects[i].effect.json(), st->effects[i].effect.json()));
            check_eq(theme::write_style(b), text);
        }
    });

    s.test("a style's corners and shadows: continuous curvature and an ambient layer, carried into the theme and written back; out of range refused", [&] {
        const char* smooth = R"toml(
[style.smooth]
theme = "light"
corner_smoothing = 0.6
[style.smooth.shadows]
lg = { x = 0, y = 12, blur = 24, spread = -6, color = "#0f172a2e", ambient = { x = 0, y = 2, blur = 48, spread = 0, color = "#0f172a14" } }
control_accent = { x = 0, y = 2, blur = 4, spread = -1, color = "#0f172a38", tint = 0.6 }
)toml";
        auto added = lib.add_styles(smooth, "smooth.style.omui.toml");
        check(added.has_value(), added ? "" : all(added.error()));
        auto t = lib.compose_style("smooth");
        check(t.has_value() && t->corner_smoothing == 0.6f && t->radii(12) == CornerRadii::all(12).smoothed(0.6f), "the theme's corners are smoothed");
        if (const auto st = lib.find_style("smooth")) {
            const std::string text = theme::write_style(*st);
            check(text.contains("corner_smoothing = 0.6") && text.contains("ambient = { x = 0, y = 2, blur = 48") && text.contains("tint = 0.6"), text);
        }
        // Shadows in layers: the key light's, and under it the ambient one.
        if (t) {
            const auto lg = t->shadows.find("lg");
            check(lg != t->shadows.end() && lg->second.blur == 24 && lg->second.ambient && lg->second.ambient->blur == 48, "the lg shadow has its ambient layer");
        }
        // A shadow that takes the colour of what casts it.
        if (t) {
            const auto accent = t->shadows.find("control_accent");
            check(accent != t->shadows.end() && accent->second.tint == 0.6f, "the accent shadow is tinted");
        }
        auto bad = lib.add_styles("[style.rough]\ntheme = \"light\"\ncorner_smoothing = 2\n", "rough.style.omui.toml");
        check(!bad.has_value(), "2 is out of range");
        auto overtinted = lib.add_styles("[style.ink2]\ntheme = \"light\"\n[style.ink2.shadows]\nsm = { tint = 1.5 }\n", "ink2.style.omui.toml");
        check(!overtinted.has_value(), "a tint over 1 is refused");
    });

    s.test("the defaults' finish (B4): continuous corners, shadows in two layers, controls' depth by state; dark tuned on its own", [&] {
        auto t = lib.compose(L({"light"}));
        check(t.has_value(), t ? "" : t.error());
        if (!t) return;
        check(t->corner_smoothing == 0.6f);
        for (const char* name : {"sm", "md", "lg", "control", "control:hovered", "control:pressed", "control_accent", "thumb", "thumb:pressed", "switch_thumb"}) {
            const auto it = t->shadows.find(name);
            check(it != t->shadows.end() && it->second.ambient.has_value(), name);
        }
        check(t->shadows.at("control:hovered").offset.y > t->shadows.at("control").offset.y &&
                  t->shadows.at("control:pressed").offset.y < t->shadows.at("control").offset.y,
              "lifted under the pointer, tighter pressed");
        check(t->shadows.at("control_accent").tint > 0, "an accent control's shadow takes its colour");
        auto dark = lib.compose(L({"dark"}));
        check(dark && dark->shadows.at("md").color.a > t->shadows.at("md").color.a && dark->shadows.at("md").color.r == 0,
              "dark: black and stronger, not the light theme's");
    });

    s.test("a style's motions (universal motion §5.2): over the framework's rules, a slot's for every node they move, a role's own; candidates; mistakes refused", [&] {
        const char* ink = R"toml(
[style.inky]
theme = "light"
[style.inky.motions]
update        = ["ink", "simple"]
state         = "simple"
"role:dialog" = { enter = { use = "spring", appear = { presence = "rise_in", distance = 12, also = "settle_in" } } }
)toml";
        auto added = lib.add_styles(ink, "inky.style.omui.toml");
        check(added.has_value(), added ? "" : all(added.error()));
        const auto st = lib.find_style("inky");
        check(st && st->motions.size() == 3, "three motions read");
        auto t = lib.compose_style("inky");
        check(t.has_value(), t ? "" : t.error());
        if (t) {
            const auto update = std::ranges::find_if(t->motion_rules, [](const MotionRule& r) { return r.layer == MotionRule::Layer::style && r.target == "*" && r.slot == "update"; });
            check(update != t->motion_rules.end() && update->spec.name == "ink" && update->instead.size() == 1 && update->instead[0].name == "simple", "the style applies ink to every node, with a simple fallback");
            const auto state = std::ranges::find_if(t->motion_rules, [](const MotionRule& r) { return r.layer == MotionRule::Layer::style && r.target == "*" && r.slot == "state"; });
            check(state != t->motion_rules.end() && state->spec.name == "simple", "state feedback is the style's own slot");
            check(std::ranges::any_of(t->motion_rules, [](const MotionRule& r) { return r.layer == MotionRule::Layer::framework && r.target == "checkbox" && r.spec.name == "default.check"; }), "typed defaults remain available under the style layer");
            const auto dialog = std::ranges::find_if(t->motion_rules, [](const MotionRule& r) { return r.target == "role:dialog" && r.slot == "enter"; });
            check(dialog != t->motion_rules.end() && dialog->spec.name == "spring" && dialog->spec.appear.away.offset.y == 12 && dialog->spec.appear.away.scale.x == .96 &&
                      dialog->spec.appear.name == "rise_in+settle_in" && dialog->layer == MotionRule::Layer::style_targeted,
                  dialog == t->motion_rules.end() ? std::string("the dialog's enter: none") : std::format("the dialog's enter: {} {}", dialog->spec.name, dialog->spec.appear.name));
        }
        // Written out and read back: the same motions.
        if (st) {
            const std::string text = theme::write_style(*st);
            auto again = theme::read_styles(text, "again.style.omui.toml");
            check(again && again->front().motions.size() == 3 && theme::write_style(again->front()) == text, text);
        }
        auto wrong = theme::read_styles("[style.w]\ntheme = \"light\"\n[style.w.motions]\nupdtae = \"ink\"\nbutton = { later = \"ink\" }\n", "w.style.omui.toml");
        check(!wrong.has_value() && all(wrong.error()).contains("`updtae` is not a slot") && all(wrong.error()).contains("`later` is not a slot"),
              wrong ? "accepted" : all(wrong.error()));
        check(lib.add_styles("[style.v]\ntheme = \"light\"\n[style.v.motions]\nupdate = \"swirl\"\n", "v.style.omui.toml").has_value());
        auto unknown = lib.compose_style("v");
        check(!unknown.has_value() && unknown.error().contains("`\"swirl\"` is not a motion"), unknown ? "composed" : unknown.error());
    });

    return s.run(argc, argv);
}
