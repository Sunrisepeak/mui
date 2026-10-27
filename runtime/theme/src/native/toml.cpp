// Reading *.theme.omui.toml: toml++ reports a malformed file by throwing,
// which this unit — the library's adapter — turns into a diagnostic.
module openmui.theme;

import std;
import tomlplusplus;
import openmui.core;
import openmui.view;
import openmui.render;
import openmui.anim;

namespace omui::theme {

namespace {

constexpr std::array<std::string_view, 11> theme_keys{"doc",     "dark",   "extends", "overlay", "density", "colors",
                                                      "text",    "spacing", "radius", "shadows", "motion"};
constexpr std::array<std::string_view, 5> text_keys{"size", "weight", "line_height", "letter_spacing", "families"};
constexpr std::array<std::string_view, 5> shadow_keys{"x", "y", "blur", "spread", "color"};
constexpr std::array<std::string_view, 10> style_keys{"doc", "theme", "crossfade", "effects", "colors", "text", "spacing", "radius", "shadows", "motion"};
constexpr std::array<std::string_view, 4> effect_control_keys{"effect", "duration", "delay", "ease"};

std::size_t distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    std::iota(row.begin(), row.end(), std::size_t{0});
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t prev = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t cur = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, prev + (a[i - 1] == b[j - 1] ? 0 : 1)});
            prev = cur;
        }
    }
    return row[b.size()];
}

template <std::size_t N>
std::string did_you_mean(std::string_view name, const std::array<std::string_view, N>& options) {
    std::string_view best;
    // Within a third of the name, and never a whole one-letter name: `l` is
    // not a typo of `s`.
    std::size_t d = std::min(std::max<std::size_t>(1, name.size() / 3) + 1, name.size());
    for (std::string_view o : options)
        if (const std::size_t x = distance(name, o); x < d) {
            d = x;
            best = o;
        }
    if (!best.empty()) return std::format("did you mean `{}`?", best);
    std::string all;
    for (std::string_view o : options) all += std::format("{}{}", all.empty() ? "" : " ", o);
    return std::format("the keys are: {}", all);
}

struct Reader {
    std::string  file;
    Diagnostics& out;

    void error(const toml::node* n, std::string message, std::string hint = {}) {
        int line = 0, column = 0;
        if (n) {
            const auto s = n->source();
            line = static_cast<int>(s.begin.line);
            column = static_cast<int>(s.begin.column);
        }
        out.push_back({file, line, column, std::move(message), std::move(hint)});
    }

    std::optional<float> number(const toml::node& n, std::string_view what, float min = -1e9f) {
        auto v = n.value<double>();
        if (!v) {
            error(&n, std::format("{} must be a number", what));
            return std::nullopt;
        }
        if (*v < min) {
            error(&n, std::format("{} must be {} or more, not {}", what, min, *v));
            return std::nullopt;
        }
        return static_cast<float>(*v);
    }

    // #rgb, #rrggbb, #rrggbbaa, or as CSS writes an exact alpha: "#0f172a / 0.08".
    std::optional<Color> color(const toml::node& n, std::string_view what) {
        auto s = n.value<std::string>();
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
            error(&n, std::format("{} must be a colour", what), "#rgb, #rrggbb, #rrggbbaa, or an exact alpha: #0f172a / 0.08");
            return std::nullopt;
        }
        return Color{c->r, c->g, c->b, alpha >= 0 ? alpha : c->a};
    }

    template <std::size_t N>
    void only(const toml::table& t, const std::array<std::string_view, N>& keys, std::string_view where) {
        for (auto&& [k, v] : t)
            if (std::ranges::find(keys, k.str()) == keys.end())
                error(&v, std::format("`{}` is not a key of {}", k.str(), where), did_you_mean(k.str(), keys));
    }

    std::optional<TextPatch> text(const toml::node& n, std::string_view name) {
        const toml::table* t = n.as_table();
        if (!t) {
            error(&n, std::format("text style `{}` must be a table {{ size = …, weight = … }}", name));
            return std::nullopt;
        }
        only(*t, text_keys, std::format("a text style (`{}`)", name));
        TextPatch p;
        if (auto* x = t->get("size")) p.size = number(*x, "size", 1);
        if (auto* x = t->get("weight")) p.weight = number(*x, "weight", 1);
        if (auto* x = t->get("line_height")) p.line_height = number(*x, "line_height", 0.5f);
        if (auto* x = t->get("letter_spacing")) p.letter_spacing = number(*x, "letter_spacing");
        if (auto* x = t->get("families")) {
            std::vector<std::string> f;
            if (const toml::array* a = x->as_array())
                for (const toml::node& e : *a)
                    if (auto s = e.value<std::string>()) f.push_back(*s);
            if (f.empty()) error(x, "families must be a list of font family names, first preferred");
            else p.families = std::move(f);
        }
        return p;
    }

    std::optional<ShadowToken> shadow(const toml::node& n, std::string_view name) {
        const toml::table* t = n.as_table();
        if (!t) {
            error(&n, std::format("shadow `{}` must be a table {{ x, y, blur, spread, color }}", name));
            return std::nullopt;
        }
        only(*t, shadow_keys, std::format("a shadow (`{}`)", name));
        ShadowToken s;
        if (auto* x = t->get("x")) s.offset.x = number(*x, "x").value_or(0);
        if (auto* x = t->get("y")) s.offset.y = number(*x, "y").value_or(0);
        if (auto* x = t->get("blur")) s.blur = number(*x, "blur", 0).value_or(0);
        if (auto* x = t->get("spread")) s.spread = number(*x, "spread").value_or(0);
        if (auto* x = t->get("color")) s.color = color(*x, "a shadow's colour").value_or(s.color);
        return s;
    }

    // A motion token: another token's name ("motion.instant"), a tween
    // { duration (ms), easing, delay (ms) }, or a spring { stiffness, damping, mass }.
    std::optional<Transition> motion(const toml::node& n, std::string_view name) {
        if (auto s = n.value<std::string>()) {
            if (auto t = Transition::token(*s)) return t;
            error(&n, std::format("`{}` is not a motion token", *s), "motion.instant, motion.fast, motion.normal, motion.slow, motion.enter, motion.exit, motion.spring.snappy …");
            return std::nullopt;
        }
        const toml::table* t = n.as_table();
        if (!t) {
            error(&n, std::format("motion `{}` is a token's name, a tween {{ duration, easing }} or a spring {{ stiffness, damping }}", name));
            return std::nullopt;
        }
        constexpr std::array<std::string_view, 6> keys{"duration", "easing", "delay", "stiffness", "damping", "mass"};
        only(*t, keys, std::format("a motion token (`{}`)", name));
        Transition out;
        if (t->get("stiffness") || t->get("damping")) {
            anim::SpringParams p = anim::springs::standard;
            if (auto* x = t->get("stiffness")) p.stiffness = number(*x, "stiffness", 0.001f).value_or(p.stiffness);
            if (auto* x = t->get("damping")) p.damping = number(*x, "damping", 0).value_or(p.damping);
            if (auto* x = t->get("mass")) p.mass = number(*x, "mass", 0.001f).value_or(p.mass);
            out = Transition::spring_with(p);
        } else {
            const float ms = t->get("duration") ? number(*t->get("duration"), "duration (ms)", 0).value_or(0) : 0;
            anim::Ease e = anim::ease::standard;
            if (auto* x = t->get("easing")) {
                auto s = x->value<std::string>();
                auto parsed = s ? anim::parse_ease(*s) : std::nullopt;
                if (!parsed) error(x, "easing must be an easing's name or cubic-bezier(x1, y1, x2, y2)", "linear, standard, out_cubic, cubic-bezier(0.2, 0, 0, 1) …");
                else e = *parsed;
            }
            out = Transition::tween(anim::seconds(ms / 1000.0), e);
        }
        if (auto* x = t->get("delay")) out.delay = anim::seconds(number(*x, "delay (ms)", 0).value_or(0) / 1000.0);
        return out;
    }

    // A layer's tokens: colours, text styles, spacing, radii, shadows, motion.
    void tokens(const toml::table& t, Layer& l) {
        auto each = [&](std::string_view section, auto&& fn) {
            if (const toml::node* n = t.get(section)) {
                if (const toml::table* s = n->as_table())
                    for (auto&& [name, value] : *s) fn(std::string(name.str()), value);
                else
                    error(n, std::format("`{}` must be a table", section));
            }
        };
        each("colors", [&](std::string name, const toml::node& v) {
            if (auto c = color(v, std::format("colour `{}`", name))) l.colors[name] = *c;
        });
        each("text", [&](std::string name, const toml::node& v) {
            if (auto p = text(v, name)) l.text[name] = std::move(*p);
        });
        each("spacing", [&](std::string name, const toml::node& v) {
            if (auto n = number(v, std::format("spacing `{}`", name), 0)) l.spacing[name] = *n;
        });
        each("radius", [&](std::string name, const toml::node& v) {
            if (auto n = number(v, std::format("radius `{}`", name), 0)) l.radius[name] = *n;
        });
        each("shadows", [&](std::string name, const toml::node& v) {
            if (auto s = shadow(v, name)) l.shadows[name] = *s;
        });
        each("motion", [&](std::string name, const toml::node& v) {
            if (auto m = motion(v, name)) l.motion[name] = *m;
        });
    }
};

}  // namespace

std::string Diagnostic::text() const {
    return std::format("{}:{}:{}: {}{}", file, line, column, message, hint.empty() ? "" : std::format(" ({})", hint));
}

std::expected<std::vector<Layer>, Diagnostics> read(std::string_view source, std::string_view file) {
    Diagnostics problems;
    toml::table root;
    try {
        root = toml::parse(source, std::string(file));
    } catch (const toml::parse_error& e) {
        const auto s = e.source();
        problems.push_back({std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::string(e.description()), ""});
        return std::unexpected(std::move(problems));
    }
    Reader r{std::string(file), problems};
    const toml::table* themes = root["theme"].as_table();
    if (!themes) {
        r.error(&root, "no [theme.<name>] table", "a theme file declares one or more [theme.<name>]");
        return std::unexpected(std::move(problems));
    }
    for (auto&& [k, v] : root)
        if (k.str() != "theme") r.error(&v, std::format("`{}` is not a table of a theme file", k.str()), "a theme file holds [theme.<name>] tables");
    std::vector<Layer> out;
    for (auto&& [key, node] : *themes) {
        const toml::table* t = node.as_table();
        Layer l;
        l.name = std::string(key.str());
        l.file = std::string(file);
        if (!t) {
            r.error(&node, std::format("theme `{}` must be a table", l.name));
            continue;
        }
        if (l.name.empty() || !std::ranges::all_of(l.name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }))
            r.error(&node, std::format("theme name `{}` is not lower_snake_case", l.name));
        r.only(*t, theme_keys, std::format("a theme (`{}`)", l.name));
        if (auto s = (*t)["doc"].value<std::string>()) l.doc = *s;
        if (auto s = (*t)["extends"].value<std::string>()) l.extends = *s;
        if (auto b = (*t)["overlay"].value<bool>()) l.overlay = *b;
        if (auto b = (*t)["dark"].value<bool>()) l.dark = *b;
        if (const toml::node* d = t->get("density")) {
            auto v = d->value<double>();
            if (!v || *v < 0.5 || *v > 2) r.error(d, "density must be a number from 0.5 to 2", "1 is the default; 0.8 is compact");
            else l.density = static_cast<float>(*v);
        }
        if (l.overlay && !l.extends.empty())
            r.error(t->get("extends"), std::format("overlay `{}` extends nothing", l.name), "an overlay is a layer over whichever theme it is composed with");
        if (l.overlay && l.dark)
            r.error(t->get("dark"), std::format("overlay `{}` cannot say whether the theme is dark", l.name), "dark is a theme's: declare a theme that extends one");
        if (!l.extends.empty() && l.extends == l.name) r.error(t->get("extends"), std::format("theme `{}` extends itself", l.name));
        r.tokens(*t, l);
        out.push_back(std::move(l));
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return out;
}

std::expected<std::vector<Style>, Diagnostics> read_styles(std::string_view source, std::string_view file) {
    Diagnostics problems;
    toml::table root;
    try {
        root = toml::parse(source, std::string(file));
    } catch (const toml::parse_error& e) {
        const auto s = e.source();
        problems.push_back({std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::string(e.description()), ""});
        return std::unexpected(std::move(problems));
    }
    Reader r{std::string(file), problems};
    const toml::table* styles = root["style"].as_table();
    if (!styles) {
        r.error(&root, "no [style.<name>] table", "a style file declares one or more [style.<name>]");
        return std::unexpected(std::move(problems));
    }
    for (auto&& [k, v] : root)
        if (k.str() != "style") r.error(&v, std::format("`{}` is not a table of a style file", k.str()), "a style file holds [style.<name>] tables");
    std::vector<Style> out;
    for (auto&& [key, node] : *styles) {
        Style st;
        st.name = std::string(key.str());
        st.file = std::string(file);
        const toml::table* t = node.as_table();
        if (!t) {
            r.error(&node, std::format("style `{}` must be a table", st.name));
            continue;
        }
        if (st.name.empty() || !std::ranges::all_of(st.name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }))
            r.error(&node, std::format("style name `{}` is not lower_snake_case", st.name));
        r.only(*t, style_keys, std::format("a style (`{}`)", st.name));
        if (auto s = (*t)["doc"].value<std::string>()) st.doc = *s;
        if (auto s = (*t)["theme"].value<std::string>()) {
            // "dark" or "dark+compact": a theme, then overlays.
            std::string_view rest = *s;
            while (!rest.empty()) {
                const auto plus = rest.find('+');
                st.layers.emplace_back(rest.substr(0, plus));
                rest = plus == std::string_view::npos ? std::string_view{} : rest.substr(plus + 1);
            }
        } else {
            r.error(t, std::format("style `{}` names no theme to start from", st.name), "theme = \"dark\" (or \"light+compact\")");
        }
        if (const toml::node* c = t->get("crossfade")) {
            auto v = c->value<double>();
            if (!v || *v < 0 || *v > 5) r.error(c, "crossfade is a number of seconds from 0 to 5", "0.35 is the default");
            else st.crossfade = *v;
        }
        st.tokens.name = std::format("style:{}", st.name);
        st.tokens.file = st.file;
        st.tokens.overlay = true;
        r.tokens(*t, st.tokens);
        if (const toml::node* e = t->get("effects")) {
            const toml::table* et = e->as_table();
            if (!et) r.error(e, "`effects` must be a table", "window = { effect = \"crt\", curvature = 0.08 }");
            else
                for (auto&& [sel, rule] : *et) {
                    const std::string selector(sel.str());
                    const toml::table* rt = rule.as_table();
                    if (!rt) {
                        r.error(&rule, std::format("the effect for `{}` must be a table", selector), "{ effect = \"neon\", radius = 12 }");
                        continue;
                    }
                    const auto name = (*rt)["effect"].value<std::string>();
                    if (!name) {
                        r.error(&rule, std::format("the effect for `{}` names no `effect`", selector), "{ effect = \"neon\", … }");
                        continue;
                    }
                    const auto ref = render::effects().find(*name);
                    if (!ref) {
                        std::vector<std::string_view> names;
                        for (std::string_view n : render::effects().names()) names.push_back(n);
                        std::string_view best;
                        std::size_t d = std::max<std::size_t>(1, name->size() / 3) + 1;
                        for (std::string_view n : names)
                            if (const std::size_t x = distance(*name, n); x < d) d = x, best = n;
                        r.error(rt->get("effect"), std::format("no effect is named `{}`", *name),
                                best.empty() ? std::string("render::effects().names() lists them") : std::format("did you mean `{}`?", best));
                        continue;
                    }
                    const render::EffectInfo* info = render::effects().info(*ref);
                    Value effect = Value{}.set("name", Value(*name));
                    for (auto&& [pk, pv] : *rt) {
                        const std::string_view param = pk.str();
                        if (param == "effect") continue;
                        if (param == "ease") {
                            if (auto e = pv.value<std::string>()) effect.set(param, Value(*e));
                            else r.error(&pv, "`ease` is a name (\"standard\", \"emphasized\" …)");
                            continue;
                        }
                        const bool control = std::ranges::find(effect_control_keys, param) != effect_control_keys.end();
                        const bool known = control || std::ranges::any_of(info->params, [&](const render::EffectParam& ep) { return ep.name == param; });
                        if (!known) {
                            std::string all;
                            std::string_view best;
                            std::size_t d = std::max<std::size_t>(1, param.size() / 3) + 1;
                            for (const auto& ep : info->params) {
                                all += std::format("{}`{}`", all.empty() ? "" : ", ", ep.name);
                                if (const std::size_t x = distance(param, ep.name); x < d) d = x, best = ep.name;
                            }
                            r.error(&pv, std::format("the effect `{}` has no parameter `{}`", *name, param),
                                    best.empty() ? std::format("its parameters: {}", all) : std::format("did you mean `{}`?", best));
                            continue;
                        }
                        if (auto n = pv.value<double>()) effect.set(param, Value(*n));
                        else r.error(&pv, std::format("`{}` is a number", param));
                    }
                    st.effects.push_back({selector, std::move(effect)});
                }
        }
        out.push_back(std::move(st));
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return out;
}

}  // namespace omui::theme
