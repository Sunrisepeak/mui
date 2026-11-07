module omui.view;

import std;
import omui.core;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
}

namespace omui {

// Keep synthesis in the defining module: GCC 16 crashes when importing and
// instantiating the defaulted comparison with a cross-module PointPath member.
bool operator==(const MotionSpec&, const MotionSpec&) = default;

Color Theme::color(std::string_view token, Color fallback) const {
    if (token.empty()) return fallback;
    if (token.front() == '#') return spec::Color::parse(token).transform([](spec::Color c) { return Color{c.r, c.g, c.b, c.a}; }).value_or(fallback);
    auto it = colors.find(token);
    return it == colors.end() ? fallback : it->second;
}

const TextToken& Theme::text_style(std::string_view token) const {
    auto it = text.find(token);
    if (it != text.end()) return it->second;
    static const TextToken body{};
    auto b = text.find("body");
    return b == text.end() ? body : b->second;
}

float Theme::space(std::string_view token, float fallback) const {
    auto it = spacing.find(token);
    return it == spacing.end() ? fallback : it->second;
}

float Theme::corner(std::string_view token, float fallback) const {
    auto it = radius.find(token);
    return it == radius.end() ? fallback : it->second;
}

Transition Theme::transition(std::string_view token) const {
    auto it = motion.find(token);
    if (it != motion.end()) return it->second;
    return Transition::token(token).value_or(Transition::tween(hanim::seconds(0.2)));
}

namespace {

void common(Theme& t) {
    t.text["display"] = {40, 700, 1.15f, -0.5f, {"Inter"}};
    t.text["title"] = {28, 650, 1.2f, -0.3f, {"Inter"}};
    t.text["heading"] = {20, 600, 1.3f, -0.1f, {"Inter"}};
    t.text["body"] = {15, 400, 1.45f, 0, {"Inter"}};
    t.text["label"] = {14, 500, 1.3f, 0, {"Inter"}};
    t.text["caption"] = {12, 400, 1.35f, 0.1f, {"Inter"}};
    t.text["code"] = {13, 400, 1.5f, 0, {"JetBrains Mono"}};
    for (auto [k, v] : {std::pair{"none", 0.0f}, {"xs", 4.0f}, {"s", 8.0f}, {"m", 12.0f}, {"l", 16.0f}, {"xl", 24.0f}, {"xxl", 32.0f}})
        t.spacing[k] = v;
    for (auto [k, v] : {std::pair{"none", 0.0f}, {"small", 4.0f}, {"medium", 8.0f}, {"large", 12.0f}, {"xl", 16.0f}, {"full", 9999.0f}})
        t.radius[k] = v;
    for (const auto& token : spec::motion_tokens)
        if (auto tr = Transition::token(token.name)) t.motion[std::string(token.name)] = *tr;
}

}  // namespace

Theme Theme::light() {
    Theme t;
    t.name = "light";
    t.dark = false;
    auto c = [&](std::string_view k, std::uint32_t hex, float a = 1) { t.colors[std::string(k)] = Color::hex(hex, a); };
    c("background", 0xf8fafc);
    c("surface", 0xffffff);
    c("surface_variant", 0xf1f5f9);
    c("surface_pressed", 0xe2e8f0);
    c("text", 0x0f172a);
    c("text_muted", 0x64748b);
    c("text_disabled", 0x94a3b8);
    c("border", 0xcbd5e1);
    c("border_strong", 0x94a3b8);
    c("accent", 0x4f46e5);
    c("accent_hover", 0x4338ca);
    c("accent_pressed", 0x3730a3);
    c("on_accent", 0xffffff);
    c("accent_subtle", 0xe0e7ff);
    c("bar", 0xf1f5f9);       // a bar of tabs (a segmented control, a floating tab bar)
    c("tab_lens", 0xffffff);  // the lens under its selected tab
    c("focus", 0x6366f1, 0.55f);
    c("danger", 0xdc2626);
    c("on_danger", 0xffffff);
    c("success", 0x16a34a);
    c("warning", 0xd97706);
    c("selection", 0x6366f1, 0.25f);
    c("scrim", 0x0f172a, 0.45f);
    c("track", 0xe2e8f0);
    c("thumb", 0xffffff);
    c("tooltip", 0x0f172a);
    c("on_tooltip", 0xf8fafc);
    t.shadows["sm"] = {{0, 1}, 2, 0, Color::hex(0x0f172a, 0.08f)};
    t.shadows["md"] = {{0, 4}, 12, -2, Color::hex(0x0f172a, 0.12f)};
    t.shadows["lg"] = {{0, 12}, 32, -4, Color::hex(0x0f172a, 0.18f)};
    common(t);
    return t;
}

Theme Theme::dark_theme() {
    Theme t = light();
    t.name = "dark";
    t.dark = true;
    auto c = [&](std::string_view k, std::uint32_t hex, float a = 1) { t.colors[std::string(k)] = Color::hex(hex, a); };
    c("background", 0x0b1120);
    c("surface", 0x111827);
    c("surface_variant", 0x1f2937);
    c("surface_pressed", 0x374151);
    c("text", 0xf1f5f9);
    c("text_muted", 0x94a3b8);
    c("text_disabled", 0x475569);
    c("border", 0x334155);
    c("border_strong", 0x475569);
    c("accent", 0x818cf8);
    c("accent_hover", 0x6366f1);
    c("accent_pressed", 0x4f46e5);
    c("on_accent", 0x0b1120);
    c("accent_subtle", 0x312e81);
    c("bar", 0x1f2937);
    c("tab_lens", 0x374151);
    c("track", 0x334155);
    c("thumb", 0xf1f5f9);
    c("tooltip", 0xf1f5f9);
    c("on_tooltip", 0x0b1120);
    return t;
}

namespace {

constexpr std::pair<std::string_view, MotionPrimitive> primitive_names[]{
    {"match", MotionPrimitive::match_drawing}, {"glyphs", MotionPrimitive::match_glyphs},
    {"readable-text", MotionPrimitive::readable_text},
    {"fade", MotionPrimitive::fade}, {"lift", MotionPrimitive::lift}, {"panel-lift", MotionPrimitive::panel_lift},
    {"grow", MotionPrimitive::grow}, {"check-grow", MotionPrimitive::check_grow},
    {"stretch", MotionPrimitive::stretch}, {"stagger", MotionPrimitive::stagger}, {"distance", MotionPrimitive::distance},
    {"write", MotionPrimitive::write}, {"drop", MotionPrimitive::drop}, {"noise", MotionPrimitive::noise},
    {"reshape", MotionPrimitive::reshape}, {"press", MotionPrimitive::press}
};

void add_primitive(MotionSpec& m, MotionPrimitive part) {
    m.primitives |= static_cast<std::uint16_t>(part);
    switch (part) {
        case MotionPrimitive::match_drawing: m.kind = MotionSpec::Kind::morph; break;
        case MotionPrimitive::match_glyphs: m.glyphs = true; m.text = MotionSpec::TextTransition::glyphs; break;
        case MotionPrimitive::readable_text: m.glyphs = true; m.text = MotionSpec::TextTransition::automatic; break;
        case MotionPrimitive::fade: m.fade_out = 0.6; m.fade_in = 0.3; break;
        case MotionPrimitive::lift: m.rise = 8; break;
        case MotionPrimitive::panel_lift: m.rise = 20; break;
        case MotionPrimitive::grow: m.grow = 0.96f; break;
        case MotionPrimitive::check_grow: m.grow = 0.72f; m.local_presence = true; break;
        case MotionPrimitive::stretch: m.stretch = 0.35f; break;
        case MotionPrimitive::stagger: m.order = MotionSpec::Order::document; m.stagger = 0.018; break;
        case MotionPrimitive::distance: m.order = MotionSpec::Order::distance; m.stagger = 0.012; break;
        case MotionPrimitive::write: m.write = true; break;
        case MotionPrimitive::drop: m.drop = 24; break;
        case MotionPrimitive::noise: m.translation_path = hanim::PointPath::jitter(3, 40, 7); break;
        case MotionPrimitive::reshape: m.reshape = true; break;
        case MotionPrimitive::press: m.press = 0.06f; break;
    }
}

} // namespace

std::optional<MotionPrimitive> motion_primitive(std::string_view name) {
    for (const auto& [key, part] : primitive_names) if (key == name) return part;
    return std::nullopt;
}

std::string_view motion_primitive_name(MotionPrimitive primitive) {
    for (const auto& [key, part] : primitive_names) if (part == primitive) return key;
    return {};
}

MotionSpec compose_motion(std::initializer_list<MotionPrimitive> parts, Transition timing) {
    MotionSpec m; m.curve = std::move(timing); m.glyphs = false;
    for (const auto part : parts) add_primitive(m, part);
    return m;
}

std::span<const ComponentMotionRecipe> default_component_motion_recipes() {
    static constexpr ComponentMotionRecipe recipes[]{
        {"button", "default.action", "default.focus", "default.reveal", "default.reveal"},
        {"toggle", "default.toggle", "default.focus", "default.reveal", "default.reveal"},
        {"checkbox", "default.check", "default.focus", "default.reveal", "default.reveal"},
        {"radio", "default.check", "default.focus", "default.reveal", "default.reveal"},
        {"slider", "default.value", "default.focus", "default.reveal", "default.reveal"},
        {"range_slider", "default.value", "default.focus", "default.reveal", "default.reveal"},
        {"number_input", "default.count", "default.focus", "default.reveal", "default.reveal"},
        {"text_box", "none", "default.focus", "default.reveal", "default.reveal"},
        {"select", "default.selection", "default.focus", "default.reveal", "default.reveal"},
        {"tabs", "default.selection", "default.focus", "default.reveal", "default.reveal"},
        {"segmented", "default.selection", "default.focus", "default.reveal", "default.reveal"},
        {"breadcrumb", "default.structure", "default.focus", "default.reveal", "default.reveal"},
        {"pagination", "default.count", "default.focus", "default.reveal", "default.reveal"},
        {"progress", "default.value", "default.focus", "default.reveal", "default.reveal"},
        {"badge", "default.count", "default.focus", "default.check", "default.reveal"},
        {"avatar", "default.action", "default.focus", "default.check", "default.reveal"},
        {"card", "default.surface", "default.focus", "default.reveal", "default.reveal"},
        {"accordion", "default.structure", "default.focus", "default.reveal", "default.reveal"},
        {"skeleton", "default.surface", "default.focus", "default.reveal", "default.reveal"},
        {"tooltip", "default.notice", "default.focus", "default.notice", "default.notice"},
        {"popover", "default.panel", "default.focus", "default.panel", "default.panel"},
        {"toast", "default.notice", "default.focus", "default.notice", "default.notice"},
        {"dialog", "default.panel", "default.focus", "default.panel", "default.panel"},
        {"drawer", "default.panel", "default.focus", "default.panel", "default.panel"},
        {"chip", "default.check", "default.focus", "default.check", "default.reveal"},
        {"alert", "default.notice", "default.focus", "default.notice", "default.notice"},
        {"stepper", "default.steps", "default.focus", "default.reveal", "default.reveal"},
        {"date_picker", "default.selection", "default.focus", "default.panel", "default.panel"},
        {"time_picker", "default.count", "default.focus", "default.panel", "default.panel"},
        {"color_picker", "default.surface", "default.focus", "default.panel", "default.panel"},
        {"split_view", "default.structure", "default.focus", "default.reveal", "default.reveal"},
        {"tree_view", "default.structure", "default.focus", "default.reveal", "default.reveal"},
        {"data_grid", "default.structure", "default.focus", "default.reveal", "default.reveal"},
        {"combobox", "default.selection", "default.focus", "default.panel", "default.panel"},
        {"tag_input", "default.structure", "default.focus", "default.reveal", "default.reveal"},
        {"code_editor", "none", "default.focus", "default.reveal", "default.reveal"},
        {"actor_log", "default.structure", "default.focus", "default.reveal", "default.reveal"}
    };
    return recipes;
}

std::vector<MotionRule> default_motion_rules() {
    const auto library = default_motion_library();
    auto preset = [&](std::string_view name) {
        if (name == "none") return MotionSpec{.kind = MotionSpec::Kind::none};
        const auto it = library.find(name);
        return it == library.end() ? MotionSpec{.kind = MotionSpec::Kind::none} : it->second;
    };
    std::vector<MotionRule> rules;
    // Roles are the fallback for application-defined controls; known
    // components below choose recipes for their shape and purpose.
    for (std::string_view role : {"button", "toggle_button", "checkbox", "radio", "switch", "slider", "spin_button", "progress", "meter", "combo_box",
                                  "option", "list_item", "menu_item", "menu_item_checkbox", "menu_item_radio", "tab", "tree_item", "link"})
        rules.push_back({std::format("role:{}", role), "update", preset("default.action")});
    rules.push_back({"role:tab_list", "update", preset("default.selection")});
    for (std::string_view role : {"dialog", "alert_dialog", "menu", "tooltip", "list_box"})
        rules.push_back({std::format("role:{}", role), "update", preset("default.panel")});
    for (const auto& recipe : default_component_motion_recipes())
        for (const auto& [slot, name] : {std::pair{"update", recipe.update}, std::pair{"state", recipe.state},
                                       std::pair{"enter", recipe.enter}, std::pair{"exit", recipe.exit}})
            if (!name.empty()) rules.push_back({std::string(recipe.component), slot, preset(name)});
    for (auto& rule : rules) rule.layer = MotionRule::Layer::framework;
    return rules;
}

std::map<std::string, MotionSpec, std::less<>> default_motion_library() {
    std::map<std::string, MotionSpec, std::less<>> lib;
    MotionSpec morph;  // a spring, the runtime's standard one
    morph.name = "morph";
    lib.emplace("morph", morph);
    MotionSpec simple;
    simple.curve = Transition::tween(hanim::seconds(0.16));
    simple.name = "simple";
    lib.emplace("simple", simple);
    MotionSpec spring;
    spring.curve = Transition::token("motion.spring.gentle").value_or(Transition::spring_with());
    spring.name = "spring";
    lib.emplace("spring", spring);
    // A drop: pulled out along its way, the shapes it passes merging with it.
    MotionSpec liquid = spring;
    liquid.name = "liquid";
    liquid.stretch = 0.6f;
    liquid.travel = MotionSpec::Travel{.effect = "metaball", .params = {{"smoothness", 0}}, .param = "smoothness", .peak = 14, .color = "accent"};
    lib.emplace("liquid", liquid);
    // A brush: unhurried, the line wavering as it goes, the far pieces a little later.
    MotionSpec ink;
    ink.name = "ink";
    ink.curve = Transition::tween(hanim::seconds(0.42));
    ink.order = MotionSpec::Order::distance;
    ink.stagger = 0.015;
    ink.travel = MotionSpec::Travel{.effect = "wobble", .params = {{"amount", 0}, {"size", 14}, {"ink", 0.7f}}, .param = "amount", .peak = 3};
    lib.emplace("ink", ink);
    using P = MotionPrimitive;
    const auto quick = Transition::token("motion.spring.snappy").value_or(Transition::spring_with());
    const auto gentle = Transition::token("motion.spring.gentle").value_or(Transition::spring_with());
    auto add = [&](std::string name, MotionSpec spec) {
        spec.name = name;
        spec.placement = MotionSpec::PlacementPolicy::follow_layout;
        lib.emplace(std::move(name), std::move(spec));
    };
    add("default.action", compose_motion({P::match_drawing, P::readable_text, P::fade}, quick));
    add("default.focus", compose_motion({P::match_drawing, P::match_glyphs, P::fade}, Transition::tween(hanim::seconds(0.10))));
    add("default.toggle", compose_motion({P::match_drawing, P::match_glyphs, P::fade, P::stretch}, quick));
    add("default.check", compose_motion({P::match_drawing, P::match_glyphs, P::fade, P::check_grow}, quick));
    add("default.selection", compose_motion({P::match_drawing, P::match_glyphs, P::stretch}, quick));
    add("default.value", compose_motion({P::match_drawing, P::match_glyphs, P::fade}, quick));
    add("default.count", compose_motion({P::match_drawing, P::match_glyphs, P::fade}, Transition::tween(hanim::seconds(0.22))));
    add("default.surface", compose_motion({P::match_drawing, P::match_glyphs, P::fade}, Transition::tween(hanim::seconds(0.18))));
    add("default.structure", compose_motion({P::match_drawing, P::match_glyphs, P::fade}, gentle));
    add("default.reveal", compose_motion({P::match_drawing, P::match_glyphs, P::fade, P::grow}, quick));
    add("default.panel", compose_motion({P::match_drawing, P::match_glyphs, P::fade, P::panel_lift, P::grow}, gentle));
    add("default.notice", compose_motion({P::match_drawing, P::match_glyphs, P::fade, P::lift, P::grow}, quick));
    add("default.steps", compose_motion({P::match_drawing, P::match_glyphs, P::fade, P::stretch}, quick));
    // The representations' recipes (convergence plan §6.8; universal motion §13.1).
    add("trace.focus", compose_motion({P::match_drawing, P::fade, P::write}, Transition::tween(hanim::seconds(0.32))));
    add("write.check", compose_motion({P::match_drawing, P::match_glyphs, P::write}, Transition::tween(hanim::seconds(0.26))));
    add("enter.drop", compose_motion({P::match_drawing, P::drop}, Transition::tween(hanim::seconds(0.7), hanim::ease::linear)));
    add("travel.noise", compose_motion({P::match_drawing, P::match_glyphs, P::fade, P::noise}, gentle));
    add("text.reshape", compose_motion({P::match_drawing, P::match_glyphs, P::fade, P::reshape}, gentle));
    add("press.squash", compose_motion({P::match_drawing, P::match_glyphs, P::fade, P::press}, Transition::tween(hanim::seconds(0.45), hanim::ease::linear)));
    return lib;
}

std::optional<MotionSpec> parse_motion(const Value& v, const Theme& theme) {
    MotionSpec m;
    auto curve = [&](std::string_view token) -> bool {
        if (token.empty()) return true;
        if (auto it = theme.motion.find(token); it != theme.motion.end()) return m.curve = it->second, true;
        if (auto t = Transition::token(token)) return m.curve = *t, true;
        return false;
    };
    // "morph", or a library's motion, as the rest starts from.
    auto use = [&](std::string_view name) -> bool {
        if (name == "morph" && !theme.motion_library.contains("morph")) return true;
        const auto it = theme.motion_library.find(name);
        if (it == theme.motion_library.end()) return false;
        m = it->second;
        return true;
    };
    if (v.type() == Value::Type::text) {
        std::string_view s = v.as_string();
        const std::size_t space = s.find(' ');
        const std::string_view name = s.substr(0, space);
        if (name == "none") return MotionSpec{.kind = MotionSpec::Kind::none};
        if (!use(name)) return std::nullopt;
        if (space != std::string_view::npos && !curve(s.substr(space + 1))) return std::nullopt;
        return m;
    }
    if (!v.map()) return std::nullopt;
    if (const Value* u = v.get("use")) {
        if (u->as_string() == "none") return MotionSpec{.kind = MotionSpec::Kind::none};
        if (!use(u->as_string())) return std::nullopt;
    }
    if (const Value* parts = v.get("compose")) {
        if (!parts->list()) return std::nullopt;
        if (!v.get("use")) m.glyphs = false;
        for (const Value& name : *parts->list()) {
            const auto part = motion_primitive(name.as_string());
            if (!part) return std::nullopt;
            add_primitive(m, *part);
        }
    }
    for (const auto& [key, destination] : {std::pair{"fade_out", &m.fade_out}, std::pair{"fade_in", &m.fade_in}})
        if (const Value* value = v.get(key)) {
            if (!value->is_number() || !std::isfinite(value->as_double())) return std::nullopt;
            *destination = std::clamp(value->as_double(), 0.0, 1.0);
        }
    if (const Value* origin = v.get("origin")) {
        if (origin->as_string() == "piece") m.local_presence = true;
        else if (origin->as_string() == "container") m.local_presence = false;
        else return std::nullopt;
    }
    if (const Value* c = v.get("curve"); c && !curve(c->as_string())) return std::nullopt;
    if (const Value* o = v.get("order")) {
        const std::string_view order = o->as_string();
        if (order == "together") m.order = MotionSpec::Order::together;
        else if (order == "document") m.order = MotionSpec::Order::document;
        else if (order == "distance") m.order = MotionSpec::Order::distance;
        else return std::nullopt;
    }
    if (const Value* p = v.get("placement")) {
        if (p->as_string() == "automatic") m.placement = MotionSpec::PlacementPolicy::automatic;
        else if (p->as_string() == "animate") m.placement = MotionSpec::PlacementPolicy::animate;
        else if (p->as_string() == "follow_layout") m.placement = MotionSpec::PlacementPolicy::follow_layout;
        else return std::nullopt;
    }
    if (const Value* st = v.get("stagger")) m.stagger = std::max(0.0, st->as_double());
    if (const Value* t = v.get("text")) {
        if (t->as_string() == "glyphs") m.text = MotionSpec::TextTransition::glyphs;
        else if (t->as_string() == "replace") m.text = MotionSpec::TextTransition::replace;
        else if (t->as_string() == "auto") m.text = MotionSpec::TextTransition::automatic;
        else return std::nullopt;
        m.glyphs = true;
    }
    if (const Value* r = v.get("text_rise")) {
        if (!r->is_number() || !std::isfinite(r->as_double())) return std::nullopt;
        m.text_rise = static_cast<float>(std::clamp(r->as_double(), 0.0, 2.0));
    }
    if (const Value* r = v.get("text_contrast")) {
        if (!r->is_number() || !std::isfinite(r->as_double())) return std::nullopt;
        const double value = r->as_double();
        if (value > 4.5) {
            constexpr Log motion_log{"motion"};
            motion_log.warn("text_contrast: ink-only policy cannot guarantee higher contrast than 4.5; choose a background policy");
            return std::nullopt;
        }
        if (value != 0 && value < 1) return std::nullopt;
        m.minimum_text_contrast = value;
    }
    if (const Value* g = v.get("glyphs")) m.glyphs = g->as_bool(true);
    if (const Value* g = v.get("meaning")) m.meaning = g->as_bool(true);
    if (const Value* r = v.get("rise")) m.rise = static_cast<float>(r->as_double());
    if (const Value* g = v.get("grow")) m.grow = static_cast<float>(std::clamp(g->as_double(1), 0.0, 4.0));
    if (const Value* st = v.get("stretch")) m.stretch = static_cast<float>(std::clamp(st->as_double(), 0.0, 2.0));
    if (const Value* path = v.get("vertex_path")) {
        hanim::PointPath parsed;
        if (path->is_null() || path->as_string() == "linear") {
            m.vertex_path = parsed;
        } else {
            const auto* fields = path->map();
            if (!fields) return std::nullopt;
            if (const auto* bend = path->get("bend")) {
                if (fields->size() != 1 || !bend->is_number() || !std::isfinite(bend->as_double())) return std::nullopt;
                parsed = hanim::PointPath::bend(bend->as_double());
            } else {
                if (fields->size() != 2) return std::nullopt;
                auto point = [](const Value* value) -> std::optional<hanim::Vec2> {
                    if (!value || !value->list() || value->list()->size() != 2) return std::nullopt;
                    const auto& list = *value->list();
                    if (!list[0].is_number() || !list[1].is_number()) return std::nullopt;
                    return hanim::Vec2{list[0].as_double(), list[1].as_double()};
                };
                const auto a = point(path->get("control1")), b = point(path->get("control2"));
                if (!a || !b) return std::nullopt;
                parsed = {*a, *b};
            }
            if (!parsed.valid()) return std::nullopt;
            m.vertex_path = parsed;
        }
    }
    if (const Value* tr = v.get("travel")) {
        if (tr->is_null() || tr->as_string() == "none") {
            m.travel.reset();
        } else {
            const Value::Map* fields = tr->map();
            const Value* effect = tr->get("effect");
            if (!fields || !effect || effect->as_string().empty()) return std::nullopt;
            MotionSpec::Travel t{.effect = std::string(effect->as_string())};
            if (const Value* p = tr->get("param")) t.param = std::string(p->as_string());
            if (const Value* p = tr->get("peak")) t.peak = static_cast<float>(p->as_double());
            if (const Value* c = tr->get("color")) t.color = std::string(c->as_string());
            for (const auto& [k, x] : *fields)
                if (k != "effect" && k != "param" && k != "peak" && k != "color" && x.is_number()) t.params.emplace_back(k, static_cast<float>(x.as_double()));
            m.travel = std::move(t);
        }
    }
    // Its name, as a person chooses it: the library's it uses, unless it says its own.
    if (const Value* n = v.get("name")) m.name = std::string(n->as_string());
    return m;
}

std::optional<std::vector<MotionSpec>> parse_motions(const Value& v, const Theme& theme) {
    std::vector<MotionSpec> out;
    if (const Value::List* l = v.list()) {
        for (const Value& x : *l) {
            auto m = parse_motion(x, theme);
            if (!m) return std::nullopt;
            out.push_back(std::move(*m));
        }
        return out;
    }
    auto m = parse_motion(v, theme);
    if (!m) return std::nullopt;
    out.push_back(std::move(*m));
    return out;
}

}  // namespace omui
