module omui.gen.read;

import std;
import mlibs.json;
import omui.gen.data;
import omui.gen.model;

namespace omui::gen {

void Diagnostics::error(Loc loc, std::string message, std::string hint) {
    list.push_back({Diagnostic::Severity::error, std::move(loc), std::move(message), std::move(hint)});
}

void Diagnostics::warning(Loc loc, std::string message, std::string hint) {
    list.push_back({Diagnostic::Severity::warning, std::move(loc), std::move(message), std::move(hint)});
}

bool Diagnostics::has_errors() const noexcept {
    return std::ranges::any_of(list, [](const Diagnostic& d) { return d.severity == Diagnostic::Severity::error; });
}

std::string Diagnostics::text() const {
    std::string out;
    for (const Diagnostic& d : list) {
        out += std::format("{}:{}:{}: {}: {}\n", d.loc.file, d.loc.line, d.loc.column,
                           d.severity == Diagnostic::Severity::error ? "error" : "warning", d.message);
        if (!d.hint.empty()) out += std::format("    hint: {}\n", d.hint);
    }
    return out;
}

namespace {

std::string json_text(std::string_view s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) out += std::format("\\u{:04x}", c);
                else out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

}  // namespace

std::string Diagnostics::json() const {
    std::string out = "[";
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Diagnostic& d = list[i];
        out += std::format("{}{{\"severity\":{},\"file\":{},\"line\":{},\"column\":{},\"message\":{},\"hint\":{}}}",
                           i ? "," : "", json_text(d.severity == Diagnostic::Severity::error ? "error" : "warning"),
                           json_text(d.loc.file), d.loc.line, d.loc.column, json_text(d.message), json_text(d.hint));
    }
    return out + "]";
}

namespace {

// The words a JSON snapshot keeps beside a map's entries (D-159): `doc`,
// `note`, `<entry>_doc`, as text. Where an entry is a table, a name is never
// taken for words; in a map of arguments to types only `<entry>_doc` is.
bool documentation(std::string_view key) { return key == "doc" || key == "note" || key.ends_with("_doc"); }

using mlibs::json::Node;

struct Reader {
    const mlibs::json::Document& doc;
    Diagnostics&                 diags;

    [[nodiscard]] const std::string& file() const noexcept { return doc.file(); }
    Loc loc(const Node& n) const {
        const auto place = doc.problem(n, {});
        return {doc.file(), static_cast<int>(place.line), static_cast<int>(place.column)};
    }

    std::string str(const Node& t, std::string_view key, bool required = false) const {
        const Node n = t[key];
        if (!n) {
            if (required) diags.error(loc(t), std::format("missing `{}`", key));
            return {};
        }
        if (auto v = n.text()) return *v;
        diags.error(loc(n), std::format("`{}` must be a string", key));
        return {};
    }

    bool boolean(const Node& t, std::string_view key, bool fallback) const {
        const Node n = t[key];
        if (!n) return fallback;
        if (auto v = n.boolean()) return *v;
        diags.error(loc(n), std::format("`{}` must be true or false", key));
        return fallback;
    }

    std::optional<double> number(const Node& t, std::string_view key) const {
        const Node n = t[key];
        if (!n) return std::nullopt;
        if (auto v = n.number()) return *v;
        diags.error(loc(n), std::format("`{}` must be a number", key));
        return std::nullopt;
    }

    std::vector<std::string> strings(const Node& t, std::string_view key) const {
        std::vector<std::string> out;
        const Node n = t[key];
        if (!n) return out;
        if (!n.is_array()) {
            diags.error(loc(n), std::format("`{}` must be an array of strings", key));
            return out;
        }
        for (const Node& e : n.items()) {
            if (auto v = e.text()) out.push_back(*v);
            else diags.error(loc(e), std::format("`{}` must hold strings", key));
        }
        return out;
    }

    std::map<std::string, std::string> string_map(const Node& t, std::string_view key) const {
        std::map<std::string, std::string> out;
        const Node n = t[key];
        if (!n) return out;
        if (!n.is_object()) {
            diags.error(loc(n), std::format("`{}` must be an object", key));
            return out;
        }
        for (const auto& [k, v] : n.members()) {
            if (documentation(k)) continue;
            if (auto s = v.text()) out.emplace(k, *s);
            else diags.error(loc(v), std::format("`{}.{}` must be a string", key, k));
        }
        return out;
    }

    // In name order, as the registry and the generated code have them.
    std::vector<Arg> args(const Node& t, std::string_view key) const {
        std::vector<Arg> out;
        const Node n = t[key];
        if (!n) return out;
        if (!n.is_object()) {
            diags.error(loc(n), std::format("`{}` must be an object of name: type", key));
            return out;
        }
        for (const auto& [k, v] : n.members_by_key()) {
            if (k.ends_with("_doc")) continue;  // `doc` and `note` are arguments' names here
            if (auto s = v.text()) out.push_back({k, *s});
            else diags.error(loc(v), "an argument type must be a string");
        }
        return out;
    }

    // Each element of the list `key` as an object, with its place.
    template <class F>
    void each(const Node& root, std::string_view key, F f) const {
        const Node n = root[key];
        if (!n) return;
        if (!n.is_array()) {
            diags.error(loc(n), std::format("`{}` must be a list of objects", key));
            return;
        }
        for (const Node& e : n.items()) {
            if (e.is_object()) f(e);
            else diags.error(loc(e), std::format("an element of `{}` must be an object", key));
        }
    }

    template <class T>
    void named(T& item, const Node& t) const {
        item.name = str(t, "name", true);
        item.doc = str(t, "doc");
        item.loc = loc(t);
    }
};

// A data file as its document: a JSON snapshot (D-159), or a TypeScript
// data module evaluated (D-139). TOML is no longer read.
std::optional<mlibs::json::Document> parse(const std::filesystem::path& path, Diagnostics& diags) {
    const auto name = generic_utf8(path);
    std::expected<mlibs::json::Document, std::string> document = std::unexpected(std::string());
    if (is_data_module(path)) {  // <name>.<kind>.omui.ts
        const auto file = generic_utf8(path.filename());
        document = data_document(path, file.ends_with(".native.omui.ts") ? "native" : "contract");
    } else if (path.extension() == ".json") {
        document = json_document(path);
    } else {
        diags.error({name, 0, 0}, "not a data file this reads: a <name>.<kind>.omui.ts module, or its .json",
                    "tools/dev/toml_to_ts.py writes a TOML file as its module");
        return std::nullopt;
    }
    if (!document) {
        diags.error({name, 0, 0}, document.error());
        return std::nullopt;
    }
    return std::move(*document);
}

template <class T>
void unique_names(const std::vector<T>& list, std::string_view what, Diagnostics& diags) {
    std::set<std::string> seen;
    for (const T& item : list)
        if (!seen.insert(item.name).second) diags.error(item.loc, std::format("{} `{}` is declared twice", what, item.name));
}

bool valid_identifier(std::string_view name) {
    if (name.empty() || !(std::islower(static_cast<unsigned char>(name[0])))) return false;
    return std::ranges::all_of(name, [](char c) { return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_'; });
}

constexpr std::array<std::string_view, 9> prop_types{"bool", "i32", "i64", "f64", "string", "color", "enum", "list<string>", "any"};
constexpr std::array<std::string_view, 9> arg_types{"bool", "i32", "i64", "f64", "string", "point", "rect", "size", "any"};

// An argument's type: one of the vocabulary, or alternatives such as
// `f64 | min | max`, each a type or a symbol the component interprets (a
// word, possibly negated: `end`, `-page`).
bool valid_arg_type(std::string_view type) {
    bool some = false;
    for (auto part : type | std::views::split('|')) {
        std::string_view p(part.begin(), part.end());
        while (!p.empty() && p.front() == ' ') p.remove_prefix(1);
        while (!p.empty() && p.back() == ' ') p.remove_suffix(1);
        if (p.empty()) return false;
        some = true;
        if (std::ranges::find(prop_types, p) != prop_types.end() || std::ranges::find(arg_types, p) != arg_types.end()) continue;
        std::size_t i = p.front() == '-' ? 1 : 0;
        if (i >= p.size() || p[i] < 'a' || p[i] > 'z') return false;
        for (; i < p.size(); ++i)
            if (!((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= '0' && p[i] <= '9') || p[i] == '_')) return false;
    }
    return some;
}

void read_abi(const Node& root, Reader& r, Abi& abi) {
    abi.prefix = r.str(root, "prefix");
    r.each(root, "handle", [&](const Node& t) {
        AbiHandle h;
        r.named(h, t);
        h.kind = r.str(t, "kind", true);
        abi.handles.push_back(std::move(h));
    });
    r.each(root, "result", [&](const Node& t) {
        AbiResult x;
        r.named(x, t);
        x.value = static_cast<int>(r.number(t, "value").value_or(0));
        abi.results.push_back(std::move(x));
    });
    r.each(root, "enum", [&](const Node& t) {
        AbiEnum e;
        r.named(e, t);
        e.values = r.strings(t, "values");
        abi.enums.push_back(std::move(e));
    });
    auto fields = [&](const Node& t, std::string_view key) {
        std::vector<AbiField> out;
        for (const Node& f : t[key].items()) {
            if (!f.is_object()) continue;
            AbiField field{r.str(f, "name", true), r.str(f, "type", true), r.str(f, "doc"),
                           static_cast<int>(r.number(f, "array").value_or(0))};
            out.push_back(std::move(field));
        }
        return out;
    };
    r.each(root, "struct", [&](const Node& t) {
        AbiStruct s;
        r.named(s, t);
        s.extensible = r.boolean(t, "extensible", false);
        s.fields = fields(t, "fields");
        abi.structs.push_back(std::move(s));
    });
    auto params = [&](const Node& t) {
        std::vector<AbiParam> out;
        for (auto& f : fields(t, "params")) out.push_back({f.name, f.type});
        return out;
    };
    r.each(root, "callback", [&](const Node& t) {
        AbiCallback c;
        r.named(c, t);
        c.returns = r.str(t, "returns", true);
        c.params = params(t);
        abi.callbacks.push_back(std::move(c));
    });
    r.each(root, "function", [&](const Node& t) {
        AbiFunction f;
        r.named(f, t);
        f.params = params(t);
        abi.functions.push_back(std::move(f));
    });
}

std::string base_target(std::string_view target) {
    auto paren = target.find('(');
    return std::string(target.substr(0, paren));
}

}  // namespace

Registry read_registry(const std::filesystem::path& dir, Diagnostics& diags) {
    Registry reg;
    auto load = [&](std::string_view name) -> std::optional<mlibs::json::Document> {
        auto path = dir / name;
        if (!std::filesystem::exists(path)) {
            diags.error({generic_utf8(path), 0, 0}, "registry file is missing");
            return std::nullopt;
        }
        return parse(path, diags);
    };
    auto file_of = [&](std::string_view name) { return generic_utf8(dir / name); };

    if (auto t = load("roles.json")) {
        Reader r{*t, diags};
        const Node root = t->root();
        reg.spec = r.str(root, "spec", true);
        r.each(root, "role", [&](const Node& e) {
            Role x;
            r.named(x, e);
            x.aria = r.str(e, "aria");
            x.accesskit = r.str(e, "accesskit");
            x.interactive = r.boolean(e, "interactive", false);
            reg.roles.push_back(std::move(x));
        });
    }
    if (auto t = load("states.json")) {
        Reader r{*t, diags};
        const Node root = t->root();
        r.each(root, "state", [&](const Node& e) {
            State x;
            r.named(x, e);
            reg.states.push_back(std::move(x));
        });
    }
    if (auto t = load("effects.json")) {
        Reader r{*t, diags};
        const Node root = t->root();
        r.each(root, "effect", [&](const Node& e) {
            Effect x;
            r.named(x, e);
            x.tier = static_cast<int>(r.number(e, "tier").value_or(0));
            x.answer = r.boolean(e, "answer", false);
            reg.effects.push_back(std::move(x));
        });
    }
    if (auto t = load("origins.json")) {
        Reader r{*t, diags};
        const Node root = t->root();
        r.each(root, "origin", [&](const Node& e) {
            Origin x;
            r.named(x, e);
            reg.origins.push_back(std::move(x));
        });
    }
    if (auto t = load("actions.json")) {
        Reader r{*t, diags};
        const Node root = t->root();
        r.each(root, "action", [&](const Node& e) {
            Action x;
            r.named(x, e);
            x.effect = r.str(e, "effect", true);
            x.args = r.args(e, "args");
            reg.actions.push_back(std::move(x));
        });
    }
    if (auto t = load("extensions.json")) {
        Reader r{*t, diags};
        const Node root = t->root();
        r.each(root, "extension", [&](const Node& e) {
            Extension x;
            r.named(x, e);
            x.version = static_cast<int>(r.number(e, "version").value_or(1));
            x.requires_ = r.strings(e, "requires");
            reg.extensions.push_back(std::move(x));
        });
    }
    if (auto t = load("profiles.json")) {
        Reader r{*t, diags};
        const Node root = t->root();
        r.each(root, "profile", [&](const Node& e) {
            Profile x;
            r.named(x, e);
            x.scripts = r.strings(e, "scripts");
            x.min_scripts = static_cast<int>(r.number(e, "min_scripts").value_or(static_cast<double>(x.scripts.size())));
            reg.profiles.push_back(std::move(x));
        });
    }
    if (auto t = load("motion.json")) {
        Reader r{*t, diags};
        const Node root = t->root();
        reg.easing = r.string_map(root, "easing");
        reg.spring = r.string_map(root, "spring");
        r.each(root, "token", [&](const Node& e) {
            MotionToken x;
            r.named(x, e);
            x.duration_ms = r.number(e, "duration");
            x.easing = r.str(e, "easing");
            x.stiffness = r.number(e, "stiffness");
            x.damping = r.number(e, "damping");
            x.mass = r.number(e, "mass");
            if (!x.duration_ms && !x.stiffness)
                diags.error(x.loc, std::format("motion token `{}` has neither a duration nor a spring", x.name));
            reg.motion_tokens.push_back(std::move(x));
        });
        r.each(root, "slot", [&](const Node& e) {
            MotionSlot x;
            r.named(x, e);
            reg.motion_slots.push_back(std::move(x));
        });
        constexpr std::array<std::string_view, 16> primitives{"match_drawing", "match_glyphs", "fade", "lift", "panel_lift", "grow",
                                                             "check_grow", "stretch", "stagger", "distance", "readable_text",
                                                             "write", "drop", "noise", "reshape", "press"};
        r.each(root, "recipe", [&](const Node& e) {
            MotionRecipe x;
            r.named(x, e);
            x.primitives = r.strings(e, "primitives");
            x.timing = r.str(e, "timing");
            x.effect = r.str(e, "effect");
            for (const auto& p : x.primitives)
                if (std::ranges::find(primitives, p) == primitives.end())
                    diags.error(x.loc, std::format("motion recipe `{}` names unknown primitive `{}`", x.name, p),
                                "match_drawing, match_glyphs, fade, lift, panel_lift, grow, check_grow, stretch, stagger, distance, readable_text, "
                                "write, drop, noise, reshape, press");
            const bool none = x.name == "none";
            if (none != x.timing.empty())
                diags.error(x.loc, std::format("motion recipe `{}`: {}", x.name, none ? "`none` has no timing" : "a recipe has a timing"));
            const auto inside = [](std::string_view text, std::string_view head) {
                return text.starts_with(head) && text.ends_with(")") && text.size() > head.size() + 1;
            };
            if (!x.timing.empty() && !inside(x.timing, "tween(") && !inside(x.timing, "spring(") && !Registry::find(reg.motion_tokens, x.timing))
                diags.error(x.loc, std::format("motion recipe `{}` has timing `{}`", x.name, x.timing),
                            "a motion token's name, \"tween(ms)\" or \"spring(k, c[, m])\"");
            reg.motion_recipes.push_back(std::move(x));
        });
        constexpr std::array<std::string_view, 8> presence_options{"distance", "offset", "scale", "radians", "height", "fade", "duration", "ease"};
        r.each(root, "presence", [&](const Node& e) {
            MotionPresence x;
            r.named(x, e);
            x.kind = r.str(e, "kind");
            x.options = r.strings(e, "options");
            if (x.kind != "enter" && x.kind != "exit") diags.error(x.loc, std::format("presence `{}` has kind `{}`", x.name, x.kind), "enter or exit");
            for (const auto& o : x.options)
                if (std::ranges::find(presence_options, o) == presence_options.end())
                    diags.error(x.loc, std::format("presence `{}` names unknown option `{}`", x.name, o),
                                "distance, offset, scale, radians, height, fade, duration, ease");
            reg.motion_presences.push_back(std::move(x));
        });
    }
    if (auto t = load("expr.json")) {
        Reader r{*t, diags};
        const Node root = t->root();
        reg.expr_types = r.strings(root, "types");
        r.each(root, "operator", [&](const Node& e) {
            ExprOperator x;
            r.named(x, e);
            x.text = r.str(e, "text", true);
            x.arity = static_cast<int>(r.number(e, "arity").value_or(0));
            x.combinators = r.string_map(e, "combinators");
            reg.operators.push_back(std::move(x));
        });
        r.each(root, "function", [&](const Node& e) {
            ExprFunction x;
            r.named(x, e);
            x.signature = r.str(e, "signature", true);
            x.combinators = r.string_map(e, "combinators");
            reg.functions.push_back(std::move(x));
        });
    }
    if (auto t = load("abi.json")) {
        Reader r{*t, diags};
        const Node root = t->root();
        read_abi(root, r, reg.abi);
    }

    unique_names(reg.roles, "role", diags);
    unique_names(reg.states, "state", diags);
    unique_names(reg.effects, "effect", diags);
    unique_names(reg.origins, "origin", diags);
    unique_names(reg.actions, "action", diags);
    unique_names(reg.extensions, "extension", diags);
    unique_names(reg.motion_tokens, "motion token", diags);
    unique_names(reg.motion_recipes, "motion recipe", diags);
    unique_names(reg.motion_slots, "motion slot", diags);
    if (reg.states.size() > 32) diags.error({file_of("states.json"), 1, 1}, "more than 32 states do not fit the state set");
    for (const Action& a : reg.actions)
        if (!Registry::find(reg.effects, a.effect))
            diags.error(a.loc, std::format("action `{}` names unknown effect `{}`", a.name, a.effect));
    for (const Extension& e : reg.extensions)
        for (const std::string& dep : e.requires_)
            if (!Registry::find(reg.extensions, dep))
                diags.error(e.loc, std::format("extension `{}` requires unknown `{}`", e.name, dep));

    auto components = dir / "components";
    if (std::filesystem::is_directory(components)) {
        std::vector<std::filesystem::path> files;
        for (auto& entry : std::filesystem::directory_iterator(components))
            if (const auto name = utf8(entry.path().filename()); name.ends_with(".contract.omui.json") || name.ends_with(".contract.omui.ts"))
                files.push_back(entry.path());
        std::ranges::sort(files);
        for (auto& f : files)
            for (Contract& c : read_contracts(f, reg, diags)) reg.contracts.push_back(std::move(c));
        unique_names(reg.contracts, "contract", diags);
    }
    return reg;
}

std::vector<Contract> read_contracts(const std::filesystem::path& file, const Registry& reg, Diagnostics& diags) {
    std::vector<Contract> out;
    auto parsed = parse(file, diags);
    if (!parsed) return out;
    Reader r{*parsed, diags};
    const Node contracts = parsed->root()["contract"];
    if (!contracts.is_object()) {
        diags.error(r.loc(parsed->root()), "no `contract` object of named contracts", "{ \"contract\": { <name>: { … } } }, as defineContract evaluates");
        return out;
    }
    for (const auto& [key, t] : contracts.members_by_key()) {
        if (documentation(key) && t.is_string()) continue;
        if (!t.is_object()) continue;
        Contract c;
        c.name = key;
        c.loc = r.loc(t);
        c.doc = r.str(t, "doc");
        c.role = r.str(t, "role", true);
        c.spec = r.str(t, "spec", true);
        c.children = r.str(t, "children");
        if (c.children.empty()) c.children = "none";
        if (c.children != "none" && c.children != "any" && !valid_identifier(c.children))
            diags.error(r.loc(t["children"]), std::format("`children` is none, any, or a contract name, not `{}`", c.children));
        if (!valid_identifier(c.name))
            diags.error(c.loc, std::format("contract name `{}` is not lower_snake_case", c.name));
        if (!c.role.empty() && !Registry::find(reg.roles, c.role))
            diags.error(r.loc(t["role"]), std::format("unknown role `{}`", c.role), "see spec/registry/roles.json");

        if (const Node props = t["props"]; props.is_object()) {
            for (const auto& [pk, pt] : props.members_by_key()) {
                if (documentation(pk) && pt.is_string()) continue;
                Prop p;
                p.name = pk;
                p.loc = r.loc(pt);
                if (!pt.is_object()) {
                    diags.error(p.loc, std::format("property `{}` must be a table {{ type = …, default = … }}", p.name));
                    continue;
                }
                p.type = r.str(pt, "type", true);
                p.doc = r.str(pt, "doc");
                p.values = r.strings(pt, "values");
                p.bindable = r.boolean(pt, "bindable", false);
                if (!valid_identifier(p.name)) diags.error(p.loc, std::format("property name `{}` is not lower_snake_case", p.name));
                if (std::ranges::find(prop_types, p.type) == prop_types.end())
                    diags.error(p.loc, std::format("property `{}` has unknown type `{}`", p.name, p.type),
                                "types: bool i32 i64 f64 string color enum list<string> any");
                if (p.type == "enum" && p.values.empty())
                    diags.error(p.loc, std::format("enum property `{}` lists no values", p.name));
                if (const Node d = pt["default"]) {
                    if (auto b = d.boolean()) p.default_value = *b;
                    else if (auto i = d.integer()) p.default_value = *i;
                    else if (auto f = d.number()) p.default_value = *f;
                    else if (auto s = d.text()) p.default_value = *s;
                    else if (d.is_array()) {
                        std::vector<std::string> v;
                        for (const Node& e : d.items())
                            if (auto s2 = e.text()) v.push_back(*s2);
                        p.default_value = std::move(v);
                    }
                    bool fits = std::visit(
                        [&](const auto& v) {
                            using V = std::decay_t<decltype(v)>;
                            if constexpr (std::same_as<V, bool>) return p.type == "bool" || p.type == "any";
                            else if constexpr (std::same_as<V, std::int64_t>) return p.type == "i32" || p.type == "i64" || p.type == "f64" || p.type == "any";
                            else if constexpr (std::same_as<V, double>) return p.type == "f64" || p.type == "any";
                            else if constexpr (std::same_as<V, std::string>) {
                                if (p.type == "enum") return std::ranges::find(p.values, v) != p.values.end();
                                return p.type == "string" || p.type == "color" || p.type == "any";
                            } else if constexpr (std::same_as<V, std::vector<std::string>>) return p.type == "list<string>" || p.type == "any";
                            else return true;
                        },
                        p.default_value);
                    if (!fits) diags.error(r.loc(d), std::format("the default of `{}` does not fit its type `{}`", p.name, p.type));
                } else if (p.type == "enum" && !p.values.empty()) {
                    p.default_value = p.values.front();
                }
                c.props.push_back(std::move(p));
            }
        }

        if (const Node events = t["events"]; events.is_object()) {
            for (const auto& [ek, et] : events.members_by_key()) {
                if (documentation(ek) && et.is_string()) continue;
                Event e;
                e.name = ek;
                e.loc = r.loc(et);
                if (et.is_object()) {
                    e.args = r.args(et, "args");
                    e.coalesce = r.str(et, "coalesce");
                    e.doc = r.str(et, "doc");
                    if (!e.coalesce.empty() && e.coalesce != "frame")
                        diags.error(e.loc, std::format("event `{}`: coalesce must be \"frame\"", e.name));
                }
                for (const Arg& a : e.args)
                    if (!valid_arg_type(a.type))
                        diags.error(e.loc, std::format("event `{}` argument `{}` has unknown type `{}`", e.name, a.name, a.type));
                c.events.push_back(std::move(e));
            }
        }

        if (const Node actions = t["actions"]; actions.is_object()) {
            for (const auto& [ak, at] : actions.members_by_key()) {
                if (documentation(ak) && at.is_string()) continue;
                ContractAction a;
                a.name = ak;
                a.loc = r.loc(at);
                if (at.is_object()) {
                    a.effect = r.str(at, "effect", true);
                    a.args = r.args(at, "args");
                    for (const Arg& x : a.args)
                        if (!valid_arg_type(x.type))
                            diags.error(a.loc, std::format("action `{}` argument `{}` has unknown type `{}`", a.name, x.name, x.type),
                                        "a type (bool i32 i64 f64 string point rect size any …), or alternatives: `f64 | min | max`");
                    a.doc = r.str(at, "doc");
                    // `answer: "yes"` names the answer; `answer: true` means the usual "yes".
                    if (const auto b = at["answer"].boolean()) a.answer = *b ? "yes" : "";
                    else a.answer = r.str(at, "answer");
                }
                const Effect* eff = Registry::find(reg.effects, a.effect);
                if (!eff) diags.error(a.loc, std::format("action `{}` names unknown effect `{}`", a.name, a.effect));
                if (eff && eff->answer && a.answer.empty())
                    diags.error(a.loc, std::format("action `{}` is `{}` but states no answer (S-1)", a.name, a.effect),
                                "add answer: \"yes\" (what an agent must say to confirm it)");
                if (const Action* common = Registry::find(reg.actions, a.name)) {
                    const Effect* base = Registry::find(reg.effects, common->effect);
                    if (eff && base && eff->tier < base->tier)
                        diags.error(a.loc,
                                    std::format("action `{}` declares effect `{}`, weaker than its default `{}`", a.name,
                                                a.effect, common->effect),
                                    "a node may declare a stronger effect, never a weaker one");
                    if (a.args.empty()) a.args = common->args;
                    if (a.doc.empty()) a.doc = common->doc;
                } else if (a.doc.empty()) {
                    diags.warning(a.loc, std::format("action `{}` is not a registry action and has no doc", a.name),
                                  "add doc: \"…\" so agents know what it does");
                }
                c.actions.push_back(std::move(a));
            }
        }

        if (const Node commands = t["commands"]; commands.is_object()) {
            for (const auto& [ck, ct] : commands.members_by_key()) {
                if (documentation(ck) && ct.is_string()) continue;
                Command cmd;
                cmd.name = ck;
                cmd.loc = r.loc(ct);
                if (ct.is_object()) cmd.doc = r.str(ct, "doc");
                c.commands.push_back(std::move(cmd));
            }
        }

        if (const Node keys = t["keys"]; keys.is_object()) {
            for (const auto& [kk, kv] : keys.members_by_key()) {
                if (documentation(kk) && kv.is_string()) continue;
                Key k{kk, kv.text().value_or(""), r.loc(kv)};
                std::string base = base_target(k.target);
                bool known = std::ranges::any_of(c.actions, [&](const ContractAction& a) { return a.name == base; }) ||
                             std::ranges::any_of(c.commands, [&](const Command& x) { return x.name == base; });
                if (!known)
                    diags.error(k.loc, std::format("key `{}` names `{}`, which is neither an action nor a command of `{}`",
                                                   k.chord, base, c.name),
                                std::format("declare it among the contract `{}`'s actions or commands", c.name));
                c.keys.push_back(std::move(k));
            }
        }

        if (const Node parts = t["parts"]; parts.is_object()) {
            for (const auto& [pk, pv] : parts.members_by_key()) {
                if (documentation(pk) && pv.is_string()) continue;
                Part p;
                p.name = pk;
                p.loc = r.loc(pv);
                c.parts.push_back(std::move(p));
            }
        }

        if (const Node motion = t["motion"]; motion.is_object()) {
            for (const auto& [mk, mv] : motion.members_by_key()) {
                if (mk.ends_with("_doc") && mv.is_string()) continue;  // a part named `doc` moves as any other
                Motion m{mk, mv.text().value_or(""), r.loc(mv)};
                if (m.token != "decay" && !Registry::find(reg.motion_tokens, m.token))
                    diags.error(m.loc, std::format("motion of `{}` names unknown token `{}`", m.part, m.token),
                                "see the token entries of spec/registry/motion.json");
                c.motion.push_back(std::move(m));
            }
        }

        bool interactive = false;
        if (const Role* role = Registry::find(reg.roles, c.role)) interactive = role->interactive;
        if (interactive && c.actions.empty())
            diags.warning(c.loc, std::format("`{}` has an interactive role but declares no action", c.name));
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace omui::gen

// ── native modules (component-binding §8) ─────────────────────────────────

namespace omui::gen {

namespace {

constexpr std::array<std::string_view, 7> native_scalars{"bool", "i32", "u32", "i64", "f64", "string", "any"};

bool pascal_identifier(std::string_view s) {
    if (s.empty() || s.front() < 'A' || s.front() > 'Z') return false;
    return std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); });
}

// What is wrong with a native type (empty: nothing).
std::string native_type_problem(std::string_view type, const std::set<std::string, std::less<>>& records) {
    if (type.ends_with("?")) return native_type_problem(type.substr(0, type.size() - 1), records);
    if (type.ends_with("[]")) return native_type_problem(type.substr(0, type.size() - 2), records);
    if (std::ranges::find(native_scalars, type) != native_scalars.end() || records.contains(type)) return {};
    return std::format("unknown type `{}`", type);
}

// `name: "type"` or `name: { type: "…", doc: "…" }`, in the order the
// file writes them (their places).
std::vector<NativeParam> native_params(const Reader& r, const Node& t, std::string_view key) {
    std::vector<NativeParam> out;
    const Node m = t[key];
    if (!m) return out;
    if (!m.is_object()) {
        r.diags.error(r.loc(m), std::format("`{}` must be an object: name: \"type\", or name: {{ type: …, doc: … }}", key));
        return out;
    }
    for (const auto& [k, v] : m.members()) {
        if (k.ends_with("_doc") && v.is_string()) continue;  // `doc` and `note` are arguments' names here
        NativeParam p;
        p.name = k;
        p.loc = r.loc(v);
        if (auto s = v.text()) {
            p.type = *s;
        } else if (v.is_object()) {
            p.type = r.str(v, "type", true);
            p.doc = r.str(v, "doc");
        } else {
            r.diags.error(p.loc, std::format("`{}` is \"type\" or {{ type: …, doc: … }}", p.name));
            continue;
        }
        if (!valid_identifier(p.name)) r.diags.error(p.loc, std::format("`{}` is not lower_snake_case", p.name));
        out.push_back(std::move(p));
    }
    std::ranges::stable_sort(out, [](const NativeParam& a, const NativeParam& b) {
        return std::pair(a.loc.line, a.loc.column) < std::pair(b.loc.line, b.loc.column);
    });
    return out;
}

}  // namespace

std::vector<NativeModule> read_natives(const std::filesystem::path& file, const Registry& reg, Diagnostics& diags) {
    std::vector<NativeModule> out;
    auto parsed = parse(file, diags);
    if (!parsed) return out;
    Reader r{*parsed, diags};
    const Node natives = parsed->root()["native"];
    if (!natives.is_object()) {
        diags.error(r.loc(parsed->root()), "no `native` object of named modules", "{ \"native\": { <name>: { … } } }, as defineNative evaluates");
        return out;
    }
    for (const auto& [key, t] : natives.members_by_key()) {
        if (documentation(key) && t.is_string()) continue;
        if (!t.is_object()) continue;
        NativeModule m;
        m.name = key;
        m.loc = r.loc(t);
        m.doc = r.str(t, "doc");
        m.spec = r.str(t, "spec", true);
        if (!valid_identifier(m.name)) diags.error(m.loc, std::format("native module name `{}` is not lower_snake_case", m.name));
        if (m.doc.empty()) diags.warning(m.loc, std::format("native module `{}` has no doc", m.name), "add doc: \"…\": scripts and agents read it");

        std::set<std::string, std::less<>> records;
        for (const auto& [tk, tv] : t["types"].members()) records.emplace(tk);
        if (const Node types = t["types"]; types.is_object()) {
            for (const auto& [tk, tt] : types.members()) {
                if (documentation(tk) && tt.is_string()) continue;
                NativeType ty;
                ty.name = tk;
                ty.loc = r.loc(tt);
                if (!tt.is_object()) {
                    diags.error(ty.loc, std::format("type `{}` must be an object {{ fields: {{ … }} }}", ty.name));
                    continue;
                }
                if (!pascal_identifier(ty.name)) diags.error(ty.loc, std::format("type name `{}` is not PascalCase", ty.name));
                ty.doc = r.str(tt, "doc");
                ty.fields = native_params(r, tt, "fields");
                if (ty.fields.empty()) diags.error(ty.loc, std::format("type `{}` has no fields", ty.name));
                for (const NativeParam& f : ty.fields)
                    if (auto p = native_type_problem(f.type, records); !p.empty())
                        diags.error(f.loc, std::format("{}.{}: {}", ty.name, f.name, p), "bool i32 u32 i64 f64 string any, a type of the module, T[], T?");
                m.types.push_back(std::move(ty));
            }
        }
        if (const Node functions = t["functions"]; functions.is_object()) {
            for (const auto& [fk, ft] : functions.members()) {
                if (documentation(fk) && ft.is_string()) continue;
                NativeFunction f;
                f.name = fk;
                f.loc = r.loc(ft);
                if (!ft.is_object()) {
                    diags.error(f.loc, std::format("function `{}` must be an object {{ args: …, returns: … }}", f.name));
                    continue;
                }
                if (!valid_identifier(f.name)) diags.error(f.loc, std::format("function name `{}` is not lower_snake_case", f.name));
                f.doc = r.str(ft, "doc");
                if (f.doc.empty()) diags.warning(f.loc, std::format("function `{}` has no doc", f.name), "add doc: \"…\": scripts and agents read it");
                f.effect = r.str(ft, "effect");
                if (f.effect.empty()) f.effect = "read";
                f.async = r.boolean(ft, "async", false);
                f.params = native_params(r, ft, "args");
                f.returns = r.str(ft, "returns");
                const Effect* eff = Registry::find(reg.effects, f.effect);
                if (!eff)
                    diags.error(r.loc(ft["effect"]), std::format("function `{}` names unknown effect `{}`", f.name, f.effect));
                else if (eff->answer)
                    diags.error(r.loc(ft["effect"]), std::format("function `{}` is `{}`, which needs an answer (S-1)", f.name, f.effect),
                                "a native function cannot be confirmed: make the irreversible work a verb, which states its answer");
                for (std::size_t i = 1; i < f.params.size(); ++i)
                    if (f.params[i - 1].type.ends_with("?") && !f.params[i].type.ends_with("?"))
                        diags.error(f.params[i].loc, std::format("{}({}) follows the optional `{}`", f.name, f.params[i].name, f.params[i - 1].name),
                                    "optional arguments come last: a positional call leaves them out from the end");
                for (const NativeParam& p : f.params)
                    if (auto x = native_type_problem(p.type, records); !x.empty())
                        diags.error(p.loc, std::format("{}({}): {}", f.name, p.name, x), "bool i32 u32 i64 f64 string any, a type of the module, T[], T?");
                if (!f.returns.empty())
                    if (auto x = native_type_problem(f.returns, records); !x.empty())
                        diags.error(r.loc(ft["returns"]), std::format("{} returns: {}", f.name, x));
                m.functions.push_back(std::move(f));
            }
        }
        if (m.functions.empty()) diags.error(m.loc, std::format("native module `{}` declares no function", m.name));
        // The file's order (their places).
        auto by_place = [](const auto& a, const auto& b) { return std::pair(a.loc.line, a.loc.column) < std::pair(b.loc.line, b.loc.column); };
        std::ranges::stable_sort(m.types, by_place);
        std::ranges::stable_sort(m.functions, by_place);
        // A record holds another by value or optionally only if that one
        // does not hold it back (a list may: it holds it indirectly).
        for (const NativeType& a : m.types)
            for (const NativeParam& f : a.fields) {
                std::string_view t = f.type;
                if (t.ends_with("?")) t.remove_suffix(1);
                if (t == a.name) diags.error(f.loc, std::format("{}.{} holds a {} in itself", a.name, f.name, a.name), "a list of them (`{}[]`) may");
            }
        {
            std::set<std::string, std::less<>> placed;
            for (std::size_t pass = 0; pass <= m.types.size(); ++pass)
                for (const NativeType& a : m.types) {
                    if (placed.contains(a.name)) continue;
                    const bool ready = std::ranges::all_of(a.fields, [&](const NativeParam& f) {
                        std::string_view t = f.type;
                        if (t.ends_with("?")) t.remove_suffix(1);
                        return t.ends_with("[]") || !records.contains(t) || t == a.name || placed.contains(t);
                    });
                    if (ready) placed.insert(a.name);
                }
            std::string cycle;
            for (const NativeType& a : m.types)
                if (!placed.contains(a.name)) cycle += (cycle.empty() ? "" : ", ") + a.name;
            if (!cycle.empty()) diags.error(m.loc, std::format("records hold each other by value: {}", cycle), "make one of the fields a list (`T[]`)");
        }
        out.push_back(std::move(m));
    }
    return out;
}

}  // namespace omui::gen
