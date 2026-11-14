// Reading a theme or a style file: the JSON a `*.theme.omui.ts` or
// `*.style.omui.ts` evaluates to (D-139), every mistake at its JSON pointer
// with, where there is one, the fix.
module omui.theme;

import std;
import mlibs.json;
import mlibs.names;
import omui.core;
import omui.view;
import xrender.vector;
import hanim.core;

namespace omui::theme {

namespace {

constexpr std::array<std::string_view, 12> theme_keys{"doc",     "dark",   "extends", "overlay", "density", "colors",
                                                      "text",    "spacing", "radius", "shadows", "motion", "corner_smoothing"};
constexpr std::array<std::string_view, 5> text_keys{"size", "weight", "line_height", "letter_spacing", "families"};
constexpr std::array<std::string_view, 7> shadow_keys{"x", "y", "blur", "spread", "color", "ambient", "tint"};
constexpr std::array<std::string_view, 5> shadow_layer_keys{"x", "y", "blur", "spread", "color"};
constexpr std::array<std::string_view, 12> style_keys{"doc",    "theme",   "crossfade", "effects", "motions", "colors",
                                                      "text",   "spacing", "radius",    "shadows", "motion", "corner_smoothing"};
constexpr std::array<std::string_view, 5> effect_control_keys{"effect", "duration", "delay", "ease", "transition"};

using mlibs::names::distance;

template <std::size_t N>
std::string did_you_mean(std::string_view name, const std::array<std::string_view, N>& options) {
    return mlibs::names::nearest(name, options);
}

// A place in the document: its value and its JSON pointer.
struct At {
    const Value* value;
    std::string  pointer;

    [[nodiscard]] At operator[](std::string_view key) const {
        std::string escaped;
        for (char c : key) escaped += c == '~' ? std::string("~0") : c == '/' ? std::string("~1") : std::string(1, c);
        return {value ? value->get(key) : nullptr, std::format("{}/{}", pointer, escaped)};
    }
    [[nodiscard]] At item(std::size_t index) const {
        const Value::List* list = value ? value->list() : nullptr;
        return {list && index < list->size() ? &(*list)[index] : nullptr, std::format("{}/{}", pointer, index)};
    }
    explicit operator bool() const noexcept { return value != nullptr; }
    [[nodiscard]] const Value::Map* map() const noexcept { return value ? value->map() : nullptr; }
    [[nodiscard]] std::optional<std::string> text() const {
        if (value && value->type() == Value::Type::text) return std::string(value->as_string());
        return std::nullopt;
    }
    [[nodiscard]] std::optional<double> real() const {
        if (value && value->is_number()) return value->as_double();
        return std::nullopt;
    }
    [[nodiscard]] std::optional<bool> boolean() const {
        if (value && value->type() == Value::Type::boolean) return value->as_bool();
        return std::nullopt;
    }
    // Each member of a map, with its place.
    [[nodiscard]] std::vector<std::pair<std::string, At>> members() const {
        std::vector<std::pair<std::string, At>> out;
        if (const Value::Map* m = map())
            for (const auto& [key, _] : *m) out.emplace_back(key, (*this)[key]);
        return out;
    }
    // By key: a style's rules and motions do not depend on the order a file writes them in.
    [[nodiscard]] std::vector<std::pair<std::string, At>> members_by_key() const {
        auto out = members();
        std::ranges::stable_sort(out, {}, &std::pair<std::string, At>::first);
        return out;
    }
};

struct Reader {
    std::string_view source;
    std::string      file;
    Diagnostics&     out;

    void error(const At& at, std::string message, std::string hint = {}) {
        int line = 0, column = 0;
        if (const auto place = json_location(source, at.pointer)) {
            line = static_cast<int>(place->line);
            column = static_cast<int>(place->column);
        }
        out.push_back({file, line, column, std::move(message), std::move(hint), at.pointer});
    }

    std::optional<float> number(const At& at, std::string_view what, float min = -1e9f) {
        const auto v = at.real();
        if (!v) {
            error(at, std::format("{} must be a number", what));
            return std::nullopt;
        }
        if (*v < min) {
            error(at, std::format("{} must be {} or more, not {}", what, min, *v));
            return std::nullopt;
        }
        return static_cast<float>(*v);
    }

    // #rgb, #rrggbb, #rrggbbaa, or as CSS writes an exact alpha: "#0f172a / 0.08".
    std::optional<Color> color(const At& at, std::string_view what) {
        const auto s = at.text();
        std::optional<spec::Color> c;
        float alpha = -1;
        if (s) {
            std::string_view text = *s;
            if (const auto slash = text.find('/'); slash != std::string_view::npos) {
                std::string_view a = text.substr(slash + 1);
                while (!a.empty() && a.front() == ' ') a.remove_prefix(1);
                text = text.substr(0, slash);
                while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
                double v = -1;
                const auto [end, ec] = std::from_chars(a.data(), a.data() + a.size(), v);
                if (ec == std::errc{} && end == a.data() + a.size() && v >= 0 && v <= 1) alpha = static_cast<float>(v);
                else text = {};  // an alpha that is not one
            }
            c = spec::Color::parse(text);
        }
        if (!c) {
            error(at, std::format("{} must be a colour", what), "#rgb, #rrggbb, #rrggbbaa, or an exact alpha: #0f172a / 0.08");
            return std::nullopt;
        }
        return Color{c->r, c->g, c->b, alpha >= 0 ? alpha : c->a};
    }

    template <std::size_t N>
    void only(const At& at, const std::array<std::string_view, N>& keys, std::string_view where) {
        for (const auto& [key, member] : at.members())
            if (std::ranges::find(keys, key) == keys.end())
                error(member, std::format("`{}` is not a key of {}", key, where), did_you_mean(key, keys));
    }

    std::optional<TextPatch> text(const At& at, std::string_view name) {
        if (!at.map()) {
            error(at, std::format("text style `{}` must be an object {{ size, weight, … }}", name));
            return std::nullopt;
        }
        only(at, text_keys, std::format("a text style (`{}`)", name));
        TextPatch p;
        if (auto x = at["size"]) p.size = number(x, "size", 1);
        if (auto x = at["weight"]) p.weight = number(x, "weight", 1);
        if (auto x = at["line_height"]) p.line_height = number(x, "line_height", 0.5f);
        if (auto x = at["letter_spacing"]) p.letter_spacing = number(x, "letter_spacing");
        if (auto x = at["families"]) {
            std::vector<std::string> f;
            if (const Value::List* list = x.value->list())
                for (const Value& e : *list)
                    if (e.type() == Value::Type::text) f.emplace_back(e.as_string());
            if (f.empty()) error(x, "families must be a list of font family names, first preferred");
            else p.families = std::move(f);
        }
        return p;
    }

    std::optional<ShadowToken> shadow(const At& at, std::string_view name) {
        if (!at.map()) {
            error(at, std::format("shadow `{}` must be an object {{ x, y, blur, spread, color }}", name));
            return std::nullopt;
        }
        only(at, shadow_keys, std::format("a shadow (`{}`)", name));
        ShadowToken s;
        const auto layer = [&](const At& from, auto& into) {
            if (auto x = from["x"]) into.offset.x = number(x, "x").value_or(0);
            if (auto x = from["y"]) into.offset.y = number(x, "y").value_or(0);
            if (auto x = from["blur"]) into.blur = number(x, "blur", 0).value_or(0);
            if (auto x = from["spread"]) into.spread = number(x, "spread").value_or(0);
            if (auto x = from["color"]) into.color = color(x, "a shadow's colour").value_or(into.color);
        };
        layer(at, s);
        if (auto x = at["tint"]) {
            const auto tint = number(x, "tint", 0).value_or(0);
            if (tint > 1) error(x, "a shadow's tint is 0 … 1: how much of the colour of what casts it the shadow takes");
            else s.tint = tint;
        }
        // Under the key light's shadow, the ambient one: wide, soft, faint.
        if (auto a = at["ambient"]) {
            if (a.map()) {
                only(a, shadow_layer_keys, std::format("a shadow's ambient layer (`{}`)", name));
                ShadowLayer ambient;
                layer(a, ambient);
                s.ambient = ambient;
            } else {
                error(a, std::format("shadow `{}`'s ambient must be an object {{ x, y, blur, spread, color }}", name));
            }
        }
        return s;
    }

    // A motion token: another token's name ("motion.instant"), a tween
    // { duration (ms), easing, delay (ms) }, or a spring { stiffness, damping, mass }.
    std::optional<Transition> motion(const At& at, std::string_view name) {
        if (auto s = at.text()) {
            if (auto t = Transition::token(*s)) return t;
            error(at, std::format("`{}` is not a motion token", *s), "motion.instant, motion.fast, motion.normal, motion.slow, motion.enter, motion.exit, motion.spring.snappy …");
            return std::nullopt;
        }
        if (!at.map()) {
            error(at, std::format("motion `{}` is a token's name, a tween {{ duration, easing }} or a spring {{ stiffness, damping }}", name));
            return std::nullopt;
        }
        constexpr std::array<std::string_view, 6> keys{"duration", "easing", "delay", "stiffness", "damping", "mass"};
        only(at, keys, std::format("a motion token (`{}`)", name));
        Transition out;
        if (at["stiffness"] || at["damping"]) {
            hanim::SpringParams p = hanim::springs::standard;
            if (auto x = at["stiffness"]) p.stiffness = number(x, "stiffness", 0.001f).value_or(p.stiffness);
            if (auto x = at["damping"]) p.damping = number(x, "damping", 0).value_or(p.damping);
            if (auto x = at["mass"]) p.mass = number(x, "mass", 0.001f).value_or(p.mass);
            out = Transition::spring_with(p);
        } else {
            const float ms = at["duration"] ? number(at["duration"], "duration (ms)", 0).value_or(0) : 0;
            hanim::Ease e = hanim::ease::standard;
            if (auto x = at["easing"]) {
                const auto s = x.text();
                auto parsed = s ? hanim::parse_ease(*s) : std::nullopt;
                if (!parsed) error(x, "easing must be an easing's name or cubic-bezier(x1, y1, x2, y2)", "linear, standard, out_cubic, cubic-bezier(0.2, 0, 0, 1) …");
                else e = *parsed;
            }
            out = Transition::tween(hanim::seconds(ms / 1000.0), e);
        }
        if (auto x = at["delay"]) out.delay = hanim::seconds(number(x, "delay (ms)", 0).value_or(0) / 1000.0);
        return out;
    }

    // A layer's tokens: colours, text styles, spacing, radii, shadows, motion.
    void tokens(const At& at, Layer& l) {
        // Corners with continuous curvature: 0 circular, up to 1 (iOS's about 0.6).
        if (auto c = at["corner_smoothing"]) {
            const auto v = c.real();
            if (!v || *v < 0 || *v > 1) error(c, "corner_smoothing must be a number from 0 to 1", "0 is circular; about 0.6 is iOS's");
            else l.corner_smoothing = static_cast<float>(*v);
        }
        auto each = [&](std::string_view section, auto&& fn) {
            if (auto n = at[section]) {
                if (n.map())
                    for (const auto& [name, value] : n.members()) fn(name, value);
                else
                    error(n, std::format("`{}` must be an object", section));
            }
        };
        each("colors", [&](const std::string& name, const At& v) {
            if (auto c = color(v, std::format("colour `{}`", name))) l.colors[name] = *c;
        });
        each("text", [&](const std::string& name, const At& v) {
            if (auto p = text(v, name)) l.text[name] = std::move(*p);
        });
        each("spacing", [&](const std::string& name, const At& v) {
            if (auto n = number(v, std::format("spacing `{}`", name), 0)) l.spacing[name] = *n;
        });
        each("radius", [&](const std::string& name, const At& v) {
            if (auto n = number(v, std::format("radius `{}`", name), 0)) l.radius[name] = *n;
        });
        each("shadows", [&](const std::string& name, const At& v) {
            if (auto s = shadow(v, name)) l.shadows[name] = *s;
        });
        each("motion", [&](const std::string& name, const At& v) {
            if (auto m = motion(v, name)) l.motion[name] = *m;
        });
    }
};

}  // namespace

std::string Diagnostic::text() const {
    return std::format("{}:{}:{}: {}{}{}", file, line, column, at.empty() ? "" : std::format("{}: ", at), message,
                       hint.empty() ? "" : std::format(" ({})", hint));
}

namespace {

bool snake_case(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

// The document of a theme or a style file: its JSON parsed, and the one key
// it holds ("theme", "style") an object of named entries.
std::optional<Value> document(std::string_view source, Reader& r, std::string_view kind) {
    auto parsed = parse_json(source);
    if (!parsed) {
        r.out.push_back({r.file, static_cast<int>(parsed.error().line), static_cast<int>(parsed.error().column), parsed.error().message, ""});
        return std::nullopt;
    }
    const At root{&*parsed, ""};
    const std::string example = std::format(R"({{ "{}": {{ "<name>": {{ … }} }} }}, as define{}{}({{ <name>: {{ … }} }}) evaluates)", kind,
                                            static_cast<char>(kind[0] - 'a' + 'A'), kind.substr(1));
    if (!root[kind].map()) {
        r.error(root, std::format("no `{}` object of named {}s", kind, kind), example);
        return std::nullopt;
    }
    for (const auto& [key, member] : root.members())
        if (key != kind) r.error(member, std::format("`{}` is not a key of a {} file", key, kind), example);
    return std::move(*parsed);
}

}  // namespace

std::expected<std::vector<Layer>, Diagnostics> read(std::string_view source, std::string_view file) {
    Diagnostics problems;
    Reader r{source, std::string(file), problems};
    const auto parsed = document(source, r, "theme");
    if (!parsed) return std::unexpected(std::move(problems));
    std::vector<Layer> out;
    for (const auto& [name, t] : At{&*parsed, ""}["theme"].members()) {
        Layer l;
        l.name = name;
        l.file = std::string(file);
        if (!t.map()) {
            r.error(t, std::format("theme `{}` must be an object", l.name));
            continue;
        }
        if (!snake_case(l.name)) r.error(t, std::format("theme name `{}` is not lower_snake_case", l.name));
        r.only(t, theme_keys, std::format("a theme (`{}`)", l.name));
        if (auto s = t["doc"].text()) l.doc = *s;
        if (auto s = t["extends"].text()) l.extends = *s;
        if (auto b = t["overlay"].boolean()) l.overlay = *b;
        if (auto b = t["dark"].boolean()) l.dark = *b;
        if (auto d = t["density"]) {
            const auto v = d.real();
            if (!v || *v < 0.5 || *v > 2) r.error(d, "density must be a number from 0.5 to 2", "1 is the default; 0.8 is compact");
            else l.density = static_cast<float>(*v);
        }
        if (l.overlay && !l.extends.empty())
            r.error(t["extends"], std::format("overlay `{}` extends nothing", l.name), "an overlay is a layer over whichever theme it is composed with");
        if (l.overlay && l.dark)
            r.error(t["dark"], std::format("overlay `{}` cannot say whether the theme is dark", l.name), "dark is a theme's: declare a theme that extends one");
        if (!l.extends.empty() && l.extends == l.name) r.error(t["extends"], std::format("theme `{}` extends itself", l.name));
        r.tokens(t, l);
        out.push_back(std::move(l));
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return out;
}

std::expected<std::vector<Style>, Diagnostics> read_styles(std::string_view source, std::string_view file) {
    Diagnostics problems;
    Reader r{source, std::string(file), problems};
    const auto parsed = document(source, r, "style");
    if (!parsed) return std::unexpected(std::move(problems));
    std::vector<Style> out;
    for (const auto& [name, t] : At{&*parsed, ""}["style"].members()) {
        Style st;
        st.name = name;
        st.file = std::string(file);
        if (!t.map()) {
            r.error(t, std::format("style `{}` must be an object", st.name));
            continue;
        }
        if (!snake_case(st.name)) r.error(t, std::format("style name `{}` is not lower_snake_case", st.name));
        r.only(t, style_keys, std::format("a style (`{}`)", st.name));
        if (auto s = t["doc"].text()) st.doc = *s;
        if (auto s = t["theme"].text()) {
            // "dark" or "dark+compact": a theme, then overlays.
            std::string_view rest = *s;
            while (!rest.empty()) {
                const auto plus = rest.find('+');
                st.layers.emplace_back(rest.substr(0, plus));
                rest = plus == std::string_view::npos ? std::string_view{} : rest.substr(plus + 1);
            }
        } else {
            r.error(t, std::format("style `{}` names no theme to start from", st.name), R"(theme: "dark" (or "light+compact"))");
        }
        if (auto c = t["crossfade"]) {
            const auto v = c.real();
            if (!v || *v < 0 || *v > 5) r.error(c, "crossfade is a number of seconds from 0 to 5", "0.35 is the default");
            else st.crossfade = *v;
        }
        st.tokens.name = std::format("style:{}", st.name);
        st.tokens.file = st.file;
        st.tokens.overlay = true;
        r.tokens(t, st.tokens);
        if (auto e = t["effects"]) {
            if (!e.map()) r.error(e, "`effects` must be an object", R"(window: { effect: "crt", curvature: 0.08 })");
            else
                for (const auto& [selector, rule] : e.members_by_key()) {
                    if (!rule.map()) {
                        r.error(rule, std::format("the effect for `{}` must be an object", selector), R"({ effect: "neon", radius: 12 })");
                        continue;
                    }
                    const auto effect_name = rule["effect"].text();
                    if (!effect_name) {
                        r.error(rule, std::format("the effect for `{}` names no `effect`", selector), R"({ effect: "neon", … })");
                        continue;
                    }
                    const auto ref = xrender::effects().find(*effect_name);
                    if (!ref) {
                        std::string_view best;
                        std::size_t nearest = std::max<std::size_t>(1, effect_name->size() / 3) + 1;
                        for (std::string_view known : xrender::effects().names())
                            if (const std::size_t x = distance(*effect_name, known); x < nearest) nearest = x, best = known;
                        r.error(rule["effect"], std::format("no effect is named `{}`", *effect_name),
                                best.empty() ? std::string("xrender::effects().names() lists them") : std::format("did you mean `{}`?", best));
                        continue;
                    }
                    const xrender::EffectInfo* info = xrender::effects().info(*ref);
                    Value effect = Value{}.set("name", Value(*effect_name));
                    for (const auto& [param, pv] : rule.members_by_key()) {
                        if (param == "effect") continue;
                        if (param == "ease" || param == "usage") {
                            if (auto text = pv.text()) effect.set(param, Value(*text));
                            else r.error(pv, std::format("`{}` must be a string", param));
                            if (param == "usage" && effect.get(param) && effect.get(param)->as_string() != "appearance" && effect.get(param)->as_string() != "feedback")
                                r.error(pv, R"(`usage` must be "appearance" or "feedback")");
                            continue;
                        }
                        if (param == "animate") {
                            Value animation{Value::Map{}};
                            if (!pv.map() || pv.map()->empty()) r.error(pv, "`animate` must be a nonempty object: { amount: [0, 1] }");
                            else for (const auto& [channel, endpoints] : pv.members()) {
                                const Value::List* pair = endpoints.value->list();
                                const auto entry = std::ranges::find(info->params, channel, &xrender::EffectParam::name);
                                if (!pair || pair->size() != 2 || entry == info->params.end()) {
                                    r.error(endpoints, "animation needs a known effect parameter and two numeric endpoints"); continue;
                                }
                                const auto from = endpoints.item(0).real(), to = endpoints.item(1).real();
                                if (!from || !to || !std::isfinite(*from) || !std::isfinite(*to) || *from < entry->min || *from > entry->max || *to < entry->min || *to > entry->max) {
                                    r.error(endpoints, "animation endpoints must be finite numbers within the effect parameter range"); continue;
                                }
                                animation.set(channel, Value(Value::List{Value(*from), Value(*to)}));
                            }
                            if (const auto d = rule["duration"].real(); !d || !std::isfinite(*d) || *d < 0)
                                r.error(pv, "`animate` requires a finite nonnegative duration in seconds");
                            effect.set(param, std::move(animation));
                            continue;
                        }
                        if (param == "reverse") {
                            if (auto b = pv.boolean()) effect.set(param, Value(*b));
                            else r.error(pv, "`reverse` must be a boolean");
                            continue;
                        }
                        const bool control = std::ranges::find(effect_control_keys, param) != effect_control_keys.end();
                        // A colour by the theme's name (`color: "accent"`): its channels, `color_r` `color_g` `color_b`, follow the pack's colours.
                        const auto has = [&](std::string_view n) {
                            return std::ranges::any_of(info->params, [&](const xrender::EffectParam& ep) { return ep.name == n; });
                        };
                        if (has(std::format("{}_r", param)) && has(std::format("{}_g", param)) && has(std::format("{}_b", param))) {
                            if (auto c = pv.text()) effect.set(param, Value(*c));
                            else r.error(pv, std::format(R"(`{}` is a colour of the theme by name ("accent", "border" …))", param));
                            continue;
                        }
                        if (!control && !has(param)) {
                            std::string all;
                            std::string_view best;
                            std::size_t nearest = std::max<std::size_t>(1, param.size() / 3) + 1;
                            for (const auto& ep : info->params) {
                                all += std::format("{}`{}`", all.empty() ? "" : ", ", ep.name);
                                if (const std::size_t x = distance(param, ep.name); x < nearest) nearest = x, best = ep.name;
                            }
                            r.error(pv, std::format("the effect `{}` has no parameter `{}`", *effect_name, param),
                                    best.empty() ? std::format("its parameters: {}", all) : std::format("did you mean `{}`?", best));
                            continue;
                        }
                        if (auto n = pv.real()) effect.set(param, Value(*n));
                        else r.error(pv, std::format("`{}` is a number", param));
                    }
                    st.effects.push_back({selector, std::move(effect)});
                }
        }
        if (auto m = t["motions"]) {
            if (!m.map()) {
                r.error(m, "`motions` must be an object", R"(update: "liquid" (a slot's motion), "role:dialog": { enter: "spring" })");
            } else {
                auto slot_name = [](std::string_view k) { return std::ranges::find(motion_slots, k) != motion_slots.end(); };
                for (const auto& [key, v] : m.members_by_key()) {
                    if (slot_name(key)) {
                        st.motions.push_back({"", key, *v.value});
                        continue;
                    }
                    if (!v.map()) {
                        r.error(v, std::format("`{}` is not a slot; a type's or a role's motions are an object of slots", key),
                                "update, enter, exit, state, reorder, replace");
                        continue;
                    }
                    for (const auto& [slot, sv] : v.members_by_key()) {
                        if (!slot_name(slot)) {
                            r.error(sv, std::format("`{}` is not a slot", slot), "update, enter, exit, state, reorder, replace");
                            continue;
                        }
                        st.motions.push_back({key, slot, *sv.value});
                    }
                }
            }
        }
        out.push_back(std::move(st));
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return out;
}

}  // namespace omui::theme
