// Reading *.effect.omui.toml: toml++ reports a malformed file by throwing,
// which this unit — the library's adapter — turns into a diagnostic.
module openmui.render.effect;

import std;
import tomlplusplus;

namespace omui::effect {

namespace {

constexpr std::array<std::string_view, 6> root_keys{"name", "doc", "shader", "entry", "reach", "param"};
constexpr std::array<std::string_view, 5> param_keys{"name", "default", "min", "max", "doc"};
constexpr std::size_t max_params = 16;  // the template's params[4] of float4

// Edits from a to b — insertions, deletions, substitutions, and swaps of two
// neighbours (`reahc` is one edit from `reach`): optimal string alignment.
std::size_t distance(std::string_view a, std::string_view b) {
    const std::size_t w = b.size() + 1;
    std::vector<std::size_t> d((a.size() + 1) * w);
    for (std::size_t i = 0; i <= a.size(); ++i) d[i * w] = i;
    for (std::size_t j = 0; j <= b.size(); ++j) d[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t v = std::min({d[(i - 1) * w + j] + 1, d[i * w + j - 1] + 1, d[(i - 1) * w + j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) v = std::min(v, d[(i - 2) * w + j - 2] + 1);
            d[i * w + j] = v;
        }
    return d[a.size() * w + b.size()];
}

template <std::size_t N>
std::string did_you_mean(std::string_view name, const std::array<std::string_view, N>& options) {
    std::string_view best;
    std::size_t d = std::min(std::max<std::size_t>(1, name.size() / 3) + 1, name.size());
    for (std::string_view o : options)
        if (const std::size_t x = distance(name, o); x < d) {
            d = x;
            best = o;
        }
    if (!best.empty()) return std::format("did you mean `{}`?", best);
    std::string all;
    for (std::string_view o : options) all += std::format("{}`{}`", all.empty() ? "" : ", ", o);
    return std::format("the keys are {}", all);
}

// A name a view can write: lower case, digits and underscores, a letter first.
bool identifier(std::string_view s) {
    if (s.empty() || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    return std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
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

    template <std::size_t N>
    void only(const toml::table& t, const std::array<std::string_view, N>& keys, std::string_view where) {
        for (const auto& [k, v] : t)
            if (std::ranges::find(keys, k.str()) == keys.end())
                error(&v, std::format("{} has no key `{}`", where, k.str()), did_you_mean(k.str(), keys));
    }

    std::optional<std::string> text(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto s = n->value<std::string>()) return *s;
        error(n, std::format("{} must be a string", what));
        return std::nullopt;
    }

    std::optional<float> number(const toml::node* n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto v = n->value<double>()) {
            if (!std::isfinite(*v)) {
                error(n, std::format("{} must be a finite number", what));
                return std::nullopt;
            }
            return static_cast<float>(*v);
        }
        error(n, std::format("{} must be a number", what));
        return std::nullopt;
    }
};

}  // namespace

std::string Diagnostic::text() const {
    return std::format("{}:{}:{}: {}{}", file, line, column, message, hint.empty() ? "" : std::format(" ({})", hint));
}

std::expected<Declaration, Diagnostics> read(std::string_view source, std::string_view file) {
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
    r.only(root, root_keys, "an effect file");
    Declaration d;
    if (auto name = r.text(root.get("name"), "`name`")) {
        if (!identifier(*name)) r.error(root.get("name"), std::format("`{}` is not an effect name", *name), "lower case, digits and _, a letter first: `scanlines`");
        d.name = *name;
    } else if (!root.get("name")) {
        r.error(&root, "the effect has no `name`", "name = \"scanlines\": what layers call it");
    }
    if (auto doc = r.text(root.get("doc"), "`doc`")) d.doc = *doc;
    d.shader = d.name + ".slang";
    if (auto shader = r.text(root.get("shader"), "`shader`")) d.shader = *shader;
    d.entry = d.name + "_fs";
    if (auto entry = r.text(root.get("entry"), "`entry`")) d.entry = *entry;

    if (const toml::node* params = root.get("param")) {
        const toml::array* list = params->as_array();
        if (!list || !list->is_array_of_tables()) {
            r.error(params, "`param` is a list of tables", "[[param]] name = \"progress\" …, one table a parameter");
        } else {
            for (const toml::node& n : *list) {
                const toml::table& t = *n.as_table();
                r.only(t, param_keys, "a [[param]]");
                Param p;
                if (auto name = r.text(t.get("name"), "a parameter's `name`")) {
                    if (!identifier(*name)) r.error(t.get("name"), std::format("`{}` is not a parameter name", *name), "lower case, digits and _, a letter first");
                    if (std::ranges::any_of(d.params, [&](const Param& q) { return q.name == *name; }))
                        r.error(t.get("name"), std::format("a second parameter named `{}`", *name));
                    p.name = *name;
                } else if (!t.get("name")) {
                    r.error(&t, "a parameter without a `name`");
                }
                if (auto v = r.number(t.get("min"), "`min`")) p.min = *v;
                if (auto v = r.number(t.get("max"), "`max`")) p.max = *v;
                p.value = p.min;
                if (auto v = r.number(t.get("default"), "`default`")) p.value = *v;
                if (auto doc = r.text(t.get("doc"), "`doc`")) p.doc = *doc;
                if (!(p.min < p.max)) r.error(&t, std::format("`{}`: min {} is not below max {}", p.name, p.min, p.max));
                else if (p.value < p.min || p.value > p.max)
                    r.error(t.get("default") ? t.get("default") : &t, std::format("`{}`: the default {} is outside [{}, {}]", p.name, p.value, p.min, p.max));
                d.params.push_back(std::move(p));
            }
            if (d.params.size() > max_params)
                r.error(params, std::format("{} parameters; an effect has at most {}", d.params.size(), max_params),
                        "the template passes params[4] of float4");
        }
    }

    if (const toml::node* reach = root.get("reach")) {
        if (auto name = reach->value<std::string>()) {
            if (std::ranges::none_of(d.params, [&](const Param& p) { return p.name == *name; }))
                r.error(reach, std::format("`reach` names `{}`, which is not a parameter", *name), "a number of logical pixels, or a parameter's name");
            d.reach_param = *name;
        } else if (auto v = r.number(reach, "`reach`")) {
            if (*v < 0) r.error(reach, "`reach` is a distance, 0 or more");
            d.reach = *v;
        }
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return d;
}

}  // namespace omui::effect
