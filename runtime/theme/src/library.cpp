// The library of themes and overlays, and their composition.
module openmui.theme;

import std;
import openmui.core;
import openmui.view;

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

// Applies a layer's tokens. A layer over another (a theme that extends, an
// overlay) changes only tokens the theme already has: a name it does not is
// a typo, not a new token.
std::expected<void, std::string> layer_onto(const Layer& l, Theme& t, bool over) {
    auto set = [&](auto& into, std::string_view kind, const auto& from, auto&& assign) -> std::expected<void, std::string> {
        for (const auto& [k, v] : from) {
            auto it = into.find(k);
            if (over && it == into.end())
                return std::unexpected(std::format("{}: `{}` sets the {} `{}`, which the theme under it does not have{}", l.file, l.name, kind, k,
                                                   closest(k, into)));
            assign(into[k], v);
        }
        return {};
    };
    auto same = [](auto& to, const auto& from) { to = from; };
    if (l.dark) t.dark = *l.dark;
    if (l.density) t.density = *l.density;
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

}  // namespace omui::theme
