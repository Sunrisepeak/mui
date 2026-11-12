// Reading an effect's description: the JSON a `*.effect.omui.ts` evaluates
// to (D-139), every mistake at its JSON pointer, line and column.
module xrender.effect.api;

import std;
import mlibs.json;
import mlibs.names;

namespace xrender::effect {

namespace {

using mlibs::json::Node;

constexpr std::array<std::string_view, 6> effect_keys{"name", "doc", "shader", "entry", "reach", "params"};
constexpr std::array<std::string_view, 5> param_keys{"name", "default", "min", "max", "doc"};
constexpr std::size_t max_params = 16;  // the template's params[4] of float4

// A name a view can write: lower case, digits and underscores, a letter first.
bool identifier(std::string_view s) {
    if (s.empty() || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    return std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

struct Reader {
    const mlibs::json::Document& doc;
    Diagnostics&                 out;

    void error(const Node& n, std::string message, std::string hint = {}) {
        const auto p = doc.problem(n, std::move(message), std::move(hint));
        out.push_back({p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message, p.hint, p.at});
    }

    template <std::size_t N>
    void only(const Node& t, const std::array<std::string_view, N>& keys, std::string_view where) {
        for (const auto& [key, value] : t.members())
            if (std::ranges::find(keys, key) == keys.end()) error(value, std::format("{} has no key `{}`", where, key), mlibs::names::nearest(key, keys));
    }

    std::optional<std::string> text(const Node& n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto s = n.text()) return *s;
        error(n, std::format("{} must be a string", what));
        return std::nullopt;
    }

    std::optional<float> number(const Node& n, std::string_view what) {
        if (!n) return std::nullopt;
        if (auto v = n.number()) {
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
    return std::format("{}:{}:{}: {}{}{}", file, line, column, at.empty() ? "" : std::format("{}: ", at), message, hint.empty() ? "" : std::format(" ({})", hint));
}

std::expected<Declaration, Diagnostics> read(std::string_view source, std::string_view file) {
    Diagnostics problems;
    auto doc = mlibs::json::Document::parse(source, std::string(file));
    if (!doc) {
        const auto& p = doc.error();
        problems.push_back({p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message, p.hint, p.at});
        return std::unexpected(std::move(problems));
    }
    Reader r{*doc, problems};
    constexpr std::string_view shape = "{ \"effect\": { name, doc, reach, params: [ … ] } }, as defineEffect({ … }) evaluates";
    for (const auto& [key, value] : doc->root().members())
        if (key != "effect") r.error(value, std::format("an effect file has no key `{}`", key), std::string(shape));
    const Node root = doc->root()["effect"];
    if (!root.is_object()) {
        r.error(root, "an effect file is an effect: an object", std::string(shape));
        return std::unexpected(std::move(problems));
    }
    r.only(root, effect_keys, "an effect");
    Declaration d;
    if (auto name = r.text(root["name"], "`name`")) {
        if (!identifier(*name)) r.error(root["name"], std::format("`{}` is not an effect name", *name), "lower case, digits and _, a letter first: `scanlines`");
        d.name = *name;
    } else if (!root["name"]) {
        r.error(root, "the effect has no `name`", "name: \"scanlines\": what layers call it");
    }
    if (auto text = r.text(root["doc"], "`doc`")) d.doc = *text;
    d.shader = d.name + ".slang";
    if (auto shader = r.text(root["shader"], "`shader`")) d.shader = *shader;
    d.entry = d.name + "_fs";
    if (auto entry = r.text(root["entry"], "`entry`")) d.entry = *entry;

    if (const Node params = root["params"]) {
        if (!params.is_array() || !std::ranges::all_of(params.items(), [](const Node& n) { return n.is_object(); })) {
            r.error(params, "`params` is a list of objects", "params: [{ name: \"progress\", … }], one a parameter");
        } else {
            for (const Node& t : params.items()) {
                r.only(t, param_keys, "a parameter");
                Param p;
                if (auto name = r.text(t["name"], "a parameter's `name`")) {
                    if (!identifier(*name)) r.error(t["name"], std::format("`{}` is not a parameter name", *name), "lower case, digits and _, a letter first");
                    if (std::ranges::any_of(d.params, [&](const Param& q) { return q.name == *name; }))
                        r.error(t["name"], std::format("a second parameter named `{}`", *name));
                    p.name = *name;
                } else if (!t["name"]) {
                    r.error(t, "a parameter without a `name`");
                }
                if (auto v = r.number(t["min"], "`min`")) p.min = *v;
                if (auto v = r.number(t["max"], "`max`")) p.max = *v;
                p.value = p.min;
                if (auto v = r.number(t["default"], "`default`")) p.value = *v;
                if (auto text = r.text(t["doc"], "`doc`")) p.doc = *text;
                if (!(p.min < p.max)) r.error(t, std::format("`{}`: min {} is not below max {}", p.name, p.min, p.max));
                else if (p.value < p.min || p.value > p.max)
                    r.error(t["default"] ? t["default"] : t, std::format("`{}`: the default {} is outside [{}, {}]", p.name, p.value, p.min, p.max));
                d.params.push_back(std::move(p));
            }
            if (d.params.size() > max_params)
                r.error(params, std::format("{} parameters; an effect has at most {}", d.params.size(), max_params), "the template passes params[4] of float4");
        }
    }

    if (const Node reach = root["reach"]) {
        if (auto name = reach.text()) {
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

}  // namespace xrender::effect
