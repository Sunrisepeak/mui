// The library of themes and overlays, and their composition.
module omui.theme;

import std;
import omui.core;
import omui.view;

namespace omui::theme {

namespace {

std::string names_of(const std::vector<Layer>& layers, bool overlay) {
    std::vector<std::string_view> names;
    for (const Layer& l : layers)
        if (l.overlay == overlay) names.push_back(l.name);
    std::ranges::sort(names);
    std::string out;
    for (std::string_view n : names) out += std::format("{}{}", out.empty() ? "" : ", ", n);
    return out.empty() ? std::string("none") : out;
}

template <class Map>
std::string closest(std::string_view name, const Map& m) {
    std::string best;
    // Within a third of the name, and never a whole one-letter name: `l` is
    // not a typo of `s`.
    std::size_t d = std::min(std::max<std::size_t>(1, name.size() / 3) + 1, name.size());
    for (const auto& [k, v] : m) {
        // A small edit distance: a typo, not another token.
        std::vector<std::size_t> row(k.size() + 1);
        std::iota(row.begin(), row.end(), std::size_t{0});
        for (std::size_t i = 1; i <= name.size(); ++i) {
            std::size_t prev = row[0];
            row[0] = i;
            for (std::size_t j = 1; j <= k.size(); ++j) {
                const std::size_t cur = row[j];
                row[j] = std::min({row[j] + 1, row[j - 1] + 1, prev + (name[i - 1] == k[j - 1] ? 0 : 1)});
                prev = cur;
            }
        }
        if (row[k.size()] < d) {
            d = row[k.size()];
            best = k;
        }
    }
    return best.empty() ? std::string() : std::format(" (did you mean `{}`?)", best);
}

// Radii a theme need not have: where it has none, the component falls back
// to another (a button's corners are the theme's `medium` unless a pack gives
// buttons their own: Material's pill).
inline constexpr std::array<std::string_view, 1> optional_radii{"button"};

// Applies a layer's tokens. A layer over another (a theme that extends, an
// overlay) changes only tokens the theme already has, or the optional ones: a
// name it does not is a typo, not a new token.
std::expected<void, std::string> layer_onto(const Layer& l, Theme& t, bool over) {
    auto set = [&](auto& into, std::string_view kind, const auto& from, auto&& assign) -> std::expected<void, std::string> {
        for (const auto& [k, v] : from) {
            auto it = into.find(k);
            const bool optional = kind == "radius" && std::ranges::contains(optional_radii, std::string_view(k));
            if (over && it == into.end() && !optional)
                return std::unexpected(std::format("{}: `{}` sets the {} `{}`, which the theme under it does not have{}", l.file, l.name, kind, k,
                                                   closest(k, into)));
            assign(into[k], v);
        }
        return {};
    };
    auto same = [](auto& to, const auto& from) { to = from; };
    if (l.dark) t.dark = *l.dark;
    if (l.density) t.density = *l.density;
    if (l.corner_smoothing) t.corner_smoothing = *l.corner_smoothing;
    if (auto r = set(t.colors, "colour", l.colors, same); !r) return r;
    if (auto r = set(t.spacing, "spacing", l.spacing, same); !r) return r;
    if (auto r = set(t.radius, "radius", l.radius, same); !r) return r;
    if (auto r = set(t.shadows, "shadow", l.shadows, same); !r) return r;
    if (auto r = set(t.motion, "motion token", l.motion, same); !r) return r;
    return set(t.text, "text style", l.text, [](TextToken& to, const TextPatch& p) {
        if (p.size) to.size = *p.size;
        if (p.weight) to.weight = *p.weight;
        if (p.line_height) to.line_height = *p.line_height;
        if (p.letter_spacing) to.letter_spacing = *p.letter_spacing;
        if (p.families) to.families = *p.families;
    });
}

}  // namespace

std::expected<void, Diagnostics> Library::add(std::string_view source, std::string_view file) {
    auto read_ = read(source, file);
    if (!read_) return std::unexpected(std::move(read_.error()));
    std::scoped_lock lock(mutex_);
    Diagnostics clash;
    for (const Layer& l : *read_)
        for (const Layer& other : layers_)
            if (other.name == l.name && other.file != l.file)
                clash.push_back({std::string(file), 0, 0, std::format("`{}` is already declared by {}", l.name, other.file), "a theme's name is the program's"});
    if (!clash.empty()) return std::unexpected(std::move(clash));
    std::erase_if(layers_, [&](const Layer& l) { return l.file == file; });  // the file again: a saved edit
    for (Layer& l : *read_) layers_.push_back(std::move(l));
    return {};
}

std::expected<void, Diagnostics> Library::add_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    const std::u8string u8 = file.generic_u8string();
    const std::string name(reinterpret_cast<const char*>(u8.data()), u8.size());
    if (!in) return std::unexpected(Diagnostics{{name, 0, 0, "cannot read the file", ""}});
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    return add(text, name);
}

std::vector<std::string> Library::themes() const {
    std::scoped_lock lock(mutex_);
    std::vector<std::string> out;
    for (const Layer& l : layers_)
        if (!l.overlay) out.push_back(l.name);
    std::ranges::sort(out);
    return out;
}

std::vector<std::string> Library::overlays() const {
    std::scoped_lock lock(mutex_);
    std::vector<std::string> out;
    for (const Layer& l : layers_)
        if (l.overlay) out.push_back(l.name);
    std::ranges::sort(out);
    return out;
}

std::optional<Layer> Library::find(std::string_view name) const {
    std::scoped_lock lock(mutex_);
    for (const Layer& l : layers_)
        if (l.name == name) return l;
    return std::nullopt;
}

std::expected<Theme, std::string> Library::compose(std::span<const std::string> names) const {
    std::scoped_lock lock(mutex_);
    auto lookup = [&](std::string_view n) -> const Layer* {
        for (const Layer& l : layers_)
            if (l.name == n) return &l;
        return nullptr;
    };
    if (names.empty()) return std::unexpected("compose a theme: name one, then any overlays over it");
    const Layer* first = lookup(names[0]);
    if (!first) return std::unexpected(std::format("no theme is named `{}` (themes: {})", names[0], names_of(layers_, false)));
    if (first->overlay) return std::unexpected(std::format("`{}` is an overlay: name a theme first, then it (themes: {})", names[0], names_of(layers_, false)));
    std::vector<const Layer*> chain;
    for (const Layer* l = first;;) {
        if (std::ranges::find(chain, l) != chain.end()) return std::unexpected(std::format("`{}` extends itself through {}", names[0], l->name));
        chain.push_back(l);
        if (l->extends.empty()) break;
        const Layer* base = lookup(l->extends);
        if (!base) return std::unexpected(std::format("{}: `{}` extends `{}`, which no theme file declares", l->file, l->name, l->extends));
        if (base->overlay) return std::unexpected(std::format("{}: `{}` extends the overlay `{}`; a theme extends a theme", l->file, l->name, l->extends));
        l = base;
    }
    std::ranges::reverse(chain);
    Theme t;
    t.colors.clear();
    t.text.clear();
    t.spacing.clear();
    t.radius.clear();
    t.shadows.clear();
    t.motion.clear();
    for (const auto& token : spec::motion_tokens)  // the registry's, until a layer says otherwise
        if (auto tr = Transition::token(token.name)) t.motion[std::string(token.name)] = *tr;
    for (std::size_t i = 0; i < chain.size(); ++i)
        if (auto r = layer_onto(*chain[i], t, i > 0); !r) return std::unexpected(r.error());
    for (std::size_t i = 1; i < names.size(); ++i) {
        const Layer* o = lookup(names[i]);
        if (!o) return std::unexpected(std::format("no overlay is named `{}` (overlays: {})", names[i], names_of(layers_, true)));
        if (!o->overlay) return std::unexpected(std::format("`{}` is a theme, not an overlay: compose one theme, then overlays", names[i]));
        if (auto r = layer_onto(*o, t, true); !r) return std::unexpected(r.error());
    }
    t.name.clear();
    for (const std::string& n : names) t.name += (t.name.empty() ? "" : "+") + n;
    return t;
}

std::expected<void, Diagnostics> Library::add_styles(std::string_view source, std::string_view file) {
    auto read_ = read_styles(source, file);
    if (!read_) return std::unexpected(std::move(read_.error()));
    std::scoped_lock lock(mutex_);
    Diagnostics clash;
    for (const Style& st : *read_)
        for (const Style& other : styles_)
            if (other.name == st.name && other.file != st.file)
                clash.push_back({std::string(file), 0, 0, std::format("the style `{}` is already declared by {}", st.name, other.file), ""});
    if (!clash.empty()) return std::unexpected(std::move(clash));
    std::erase_if(styles_, [&](const Style& st) { return st.file == file; });
    for (Style& st : *read_) styles_.push_back(std::move(st));
    return {};
}

std::vector<std::string> Library::styles() const {
    std::scoped_lock lock(mutex_);
    std::vector<std::string> out;
    for (const Style& st : styles_) out.push_back(st.name);
    std::ranges::sort(out);
    return out;
}

std::optional<Style> Library::find_style(std::string_view name) const {
    std::scoped_lock lock(mutex_);
    for (const Style& st : styles_)
        if (st.name == name) return st;
    return std::nullopt;
}

std::expected<Theme, std::string> Library::compose_style(std::string_view name) const {
    const auto st = find_style(name);
    if (!st) {
        std::string all;
        for (const std::string& n : styles()) all += std::format("{}{}", all.empty() ? "" : ", ", n);
        return std::unexpected(std::format("no style is named `{}` (styles: {})", name, all.empty() ? std::string("none") : all));
    }
    auto t = compose(st->layers);
    if (!t) return std::unexpected(std::format("{}: the style `{}` starts from what cannot be composed: {}", st->file, name, t.error()));
    if (auto r = layer_onto(st->tokens, *t, true); !r) return std::unexpected(r.error());
    t->name = std::format("{}@{}", t->name, name);
    // Its motions over the framework's rules: the slots of every node they move first, then a type's or a role's.
    for (const bool targeted : {false, true})
        for (const StyleMotion& m : st->motions) {
            if (m.target.empty() == targeted) continue;
            auto specs = parse_motions(m.motion, *t);
            if (!specs || specs->empty())
                return std::unexpected(std::format("{}: the style `{}`: `{}` is not a motion for {}{} (none, morph, {}or {{use, curve, …}})", st->file, name,
                                                   m.motion.json(), m.target.empty() ? "" : m.target + "'s ", m.slot, [&] {
                                                       std::string names;
                                                       for (const auto& [n, _] : t->motion_library) names += n + ", ";
                                                       return names;
                                                   }()));
            t->motion_rules.push_back({.target = m.target.empty() ? "*" : m.target, .slot = m.slot,
                                       .spec = specs->front(), .instead = {specs->begin() + 1, specs->end()},
                                       .layer = targeted ? MotionRule::Layer::style_targeted : MotionRule::Layer::style});
        }
    return t;
}

namespace {

// A float as the number it was written as (0.1f is 0.1, not 0.10000000149011612).
Value number(float v) {
    double out = 0;
    const std::string text = std::format("{}", v);
    std::from_chars(text.data(), text.data() + text.size(), out);
    return Value(out);
}

Value colour(Color c) {
    auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    std::string out = std::format("#{:02x}{:02x}{:02x}", b(c.r), b(c.g), b(c.b));
    if (c.a < 1) out += std::format(" / {}", c.a);
    return Value(out);
}

Value shadow_layer(Point offset, float blur, float spread, Color color) {
    return Value(Value::Map{}).set("x", number(offset.x)).set("y", number(offset.y)).set("blur", number(blur)).set("spread", number(spread)).set("color", colour(color));
}

}  // namespace

std::string write_style(const Style& st) {
    Value style(Value::Map{});
    if (!st.doc.empty()) style.set("doc", Value(st.doc));
    std::string layers;
    for (const std::string& l : st.layers) layers += (layers.empty() ? "" : "+") + l;
    style.set("theme", Value(layers));
    style.set("crossfade", Value(st.crossfade));
    const Layer& t = st.tokens;
    if (t.corner_smoothing) style.set("corner_smoothing", number(*t.corner_smoothing));
    auto section = [&](std::string_view name, const auto& map, auto&& write) {
        if (map.empty()) return;
        Value out(Value::Map{});
        for (const auto& [key, v] : map) out.set(key, write(v));
        style.set(name, std::move(out));
    };
    section("colors", t.colors, [](Color c) { return colour(c); });
    section("text", t.text, [](const TextPatch& p) {
        Value out(Value::Map{});
        if (p.size) out.set("size", number(*p.size));
        if (p.weight) out.set("weight", number(*p.weight));
        if (p.line_height) out.set("line_height", number(*p.line_height));
        if (p.letter_spacing) out.set("letter_spacing", number(*p.letter_spacing));
        if (p.families) {
            Value::List families;
            for (const std::string& family : *p.families) families.emplace_back(family);
            out.set("families", Value(std::move(families)));
        }
        return out;
    });
    section("spacing", t.spacing, [](float v) { return number(v); });
    section("radius", t.radius, [](float v) { return number(v); });
    section("shadows", t.shadows, [](const ShadowToken& sh) {
        Value out = shadow_layer(sh.offset, sh.blur, sh.spread, sh.color);
        if (sh.tint != 0) out.set("tint", number(sh.tint));
        if (const auto& a = sh.ambient) out.set("ambient", shadow_layer(a->offset, a->blur, a->spread, a->color));
        return out;
    });
    section("motion", t.motion, [](const Transition& m) {
        Value out(Value::Map{});
        if (m.kind == Transition::Kind::spring)
            out.set("stiffness", number(m.spring.stiffness)).set("damping", number(m.spring.damping)).set("mass", number(m.spring.mass));
        else
            out.set("duration", Value(m.duration.count() * 1000)).set("easing", Value(hanim::to_string(m.ease)));
        if (m.delay.count() > 0) out.set("delay", Value(m.delay.count() * 1000));
        return out;
    });
    if (!st.effects.empty()) {
        Value effects(Value::Map{});
        for (const EffectRule& r : st.effects) {
            Value rule = Value(Value::Map{}).set("effect", r.effect.get("name") ? *r.effect.get("name") : Value(""));
            if (const Value::Map* m = r.effect.map())
                for (const auto& [k, v] : *m)
                    if (k != "name") rule.set(k, v);
            effects.set(r.selector, std::move(rule));
        }
        style.set("effects", std::move(effects));
    }
    if (!st.motions.empty()) {
        Value motions(Value::Map{});
        for (const StyleMotion& m : st.motions)
            if (m.target.empty()) motions.set(m.slot, m.motion);
        for (const StyleMotion& m : st.motions) {
            if (m.target.empty()) continue;
            Value slots = motions.get(m.target) ? *motions.get(m.target) : Value(Value::Map{});
            motions.set(m.target, std::move(slots.set(m.slot, m.motion)));
        }
        style.set("motions", std::move(motions));
    }
    return indented_json(Value(Value::Map{}).set("style", Value(Value::Map{}).set(st.name, std::move(style))));
}

Library& library() {
    static Library l;
    return l;
}

std::expected<void, std::string> apply(Runtime& rt, std::span<const std::string> layers) {
    auto t = library().compose(layers);
    if (!t) return std::unexpected(t.error());
    rt.set_theme(std::move(*t));
    return {};
}

std::expected<void, std::string> apply_style(Runtime& rt, std::string_view name, bool crossfade) {
    auto t = library().compose_style(name);
    if (!t) return std::unexpected(t.error());
    const auto st = library().find_style(name);
    if (crossfade && st->crossfade > 0) rt.crossfade(seconds(st->crossfade));
    rt.set_theme(std::move(*t));
    rt.set_effect_rules(st->effects);
    return {};
}

std::expected<void, std::string> apply_style(Runtime& rt, std::string_view name, std::string_view scope, bool crossfade) {
    auto t = library().compose_style(name);
    if (!t) return std::unexpected(t.error());
    const auto st = library().find_style(name);
    if (crossfade && st->crossfade > 0) rt.crossfade(seconds(st->crossfade), scope);
    rt.set_scope(scope, std::move(*t), st->effects);
    return {};
}

}  // namespace omui::theme
