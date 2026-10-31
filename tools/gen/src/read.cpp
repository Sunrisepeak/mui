module openmui.gen.read;

import std;
import tomlplusplus;
import openmui.gen.model;

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

struct Reader {
    std::string  file;
    Diagnostics& diags;

    Loc loc(const toml::node* n) const {
        if (!n) return {file, 0, 0};
        auto s = n->source();
        return {file, static_cast<int>(s.begin.line), static_cast<int>(s.begin.column)};
    }

    std::string str(const toml::table& t, std::string_view key, bool required = false) const {
        const toml::node* n = t.get(key);
        if (!n) {
            if (required) diags.error(loc(&t), std::format("missing `{}`", key));
            return {};
        }
        if (auto v = n->value<std::string>()) return *v;
        diags.error(loc(n), std::format("`{}` must be a string", key));
        return {};
    }

    bool boolean(const toml::table& t, std::string_view key, bool fallback) const {
        const toml::node* n = t.get(key);
        if (!n) return fallback;
        if (auto v = n->value<bool>()) return *v;
        diags.error(loc(n), std::format("`{}` must be true or false", key));
        return fallback;
    }

    std::optional<double> number(const toml::table& t, std::string_view key) const {
        const toml::node* n = t.get(key);
        if (!n) return std::nullopt;
        if (auto v = n->value<double>()) return *v;
        diags.error(loc(n), std::format("`{}` must be a number", key));
        return std::nullopt;
    }

    std::vector<std::string> strings(const toml::table& t, std::string_view key) const {
        std::vector<std::string> out;
        const toml::node* n = t.get(key);
        if (!n) return out;
        const toml::array* a = n->as_array();
        if (!a) {
            diags.error(loc(n), std::format("`{}` must be an array of strings", key));
            return out;
        }
        for (const toml::node& e : *a) {
            if (auto v = e.value<std::string>()) out.push_back(*v);
            else diags.error(loc(&e), std::format("`{}` must hold strings", key));
        }
        return out;
    }

    std::map<std::string, std::string> string_map(const toml::table& t, std::string_view key) const {
        std::map<std::string, std::string> out;
        const toml::node* n = t.get(key);
        if (!n) return out;
        const toml::table* m = n->as_table();
        if (!m) {
            diags.error(loc(n), std::format("`{}` must be a table", key));
            return out;
        }
        for (auto&& [k, v] : *m) {
            if (auto s = v.value<std::string>()) out.emplace(std::string(k.str()), *s);
            else diags.error(loc(&v), std::format("`{}.{}` must be a string", key, k.str()));
        }
        return out;
    }

    std::vector<Arg> args(const toml::table& t, std::string_view key) const {
        std::vector<Arg> out;
        const toml::node* n = t.get(key);
        if (!n) return out;
        const toml::table* m = n->as_table();
        if (!m) {
            diags.error(loc(n), std::format("`{}` must be a table of name = type", key));
            return out;
        }
        for (auto&& [k, v] : *m) {
            if (auto s = v.value<std::string>()) out.push_back({std::string(k.str()), *s});
            else diags.error(loc(&v), "an argument type must be a string");
        }
        return out;
    }

    // Each [[name]] element as a table, with its location.
    template <class F>
    void each(const toml::table& root, std::string_view key, F f) const {
        const toml::node* n = root.get(key);
        if (!n) return;
        const toml::array* a = n->as_array();
        if (!a) {
            diags.error(loc(n), std::format("`{}` must be an array of tables ([[{}]])", key, key));
            return;
        }
        for (const toml::node& e : *a) {
            if (const toml::table* t = e.as_table()) f(*t);
            else diags.error(loc(&e), std::format("an element of `{}` must be a table", key));
        }
    }

    template <class T>
    void named(T& item, const toml::table& t) const {
        item.name = str(t, "name", true);
        item.doc = str(t, "doc");
        item.loc = loc(&t);
    }
};

std::optional<toml::table> parse(const std::filesystem::path& path, Diagnostics& diags) {
    try {
        // Read through the path itself (wide on Windows), then parse the text.
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            diags.error({generic_utf8(path), 0, 0}, "cannot read the file");
            return std::nullopt;
        }
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        return toml::parse(text, generic_utf8(path));
    } catch (const toml::parse_error& e) {
        auto s = e.source();
        diags.error({generic_utf8(path), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column)},
                    std::string(e.description()));
        return std::nullopt;
    }
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

void read_abi(const toml::table& root, Reader& r, Abi& abi) {
    abi.prefix = r.str(root, "prefix");
    r.each(root, "handle", [&](const toml::table& t) {
        AbiHandle h;
        r.named(h, t);
        h.kind = r.str(t, "kind", true);
        abi.handles.push_back(std::move(h));
    });
    r.each(root, "result", [&](const toml::table& t) {
        AbiResult x;
        r.named(x, t);
        x.value = static_cast<int>(r.number(t, "value").value_or(0));
        abi.results.push_back(std::move(x));
    });
    r.each(root, "enum", [&](const toml::table& t) {
        AbiEnum e;
        r.named(e, t);
        e.values = r.strings(t, "values");
        abi.enums.push_back(std::move(e));
    });
    auto fields = [&](const toml::table& t, std::string_view key) {
        std::vector<AbiField> out;
        const toml::node* n = t.get(key);
        if (!n || !n->is_array()) return out;
        for (const toml::node& e : *n->as_array()) {
            const toml::table* f = e.as_table();
            if (!f) continue;
            AbiField field{r.str(*f, "name", true), r.str(*f, "type", true), r.str(*f, "doc"),
                           static_cast<int>(r.number(*f, "array").value_or(0))};
            out.push_back(std::move(field));
        }
        return out;
    };
    r.each(root, "struct", [&](const toml::table& t) {
        AbiStruct s;
        r.named(s, t);
        s.extensible = r.boolean(t, "extensible", false);
        s.fields = fields(t, "fields");
        abi.structs.push_back(std::move(s));
    });
    auto params = [&](const toml::table& t) {
        std::vector<AbiParam> out;
        for (auto& f : fields(t, "params")) out.push_back({f.name, f.type});
        return out;
    };
    r.each(root, "callback", [&](const toml::table& t) {
        AbiCallback c;
        r.named(c, t);
        c.returns = r.str(t, "returns", true);
        c.params = params(t);
        abi.callbacks.push_back(std::move(c));
    });
    r.each(root, "function", [&](const toml::table& t) {
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
    auto load = [&](std::string_view name) -> std::optional<toml::table> {
        auto path = dir / name;
        if (!std::filesystem::exists(path)) {
            diags.error({generic_utf8(path), 0, 0}, "registry file is missing");
            return std::nullopt;
        }
        return parse(path, diags);
    };
    auto file_of = [&](std::string_view name) { return generic_utf8(dir / name); };

    if (auto t = load("roles.toml")) {
        Reader r{file_of("roles.toml"), diags};
        reg.spec = r.str(*t, "spec", true);
        r.each(*t, "role", [&](const toml::table& e) {
            Role x;
            r.named(x, e);
            x.aria = r.str(e, "aria");
            x.accesskit = r.str(e, "accesskit");
            x.interactive = r.boolean(e, "interactive", false);
            reg.roles.push_back(std::move(x));
        });
    }
    if (auto t = load("states.toml")) {
        Reader r{file_of("states.toml"), diags};
        r.each(*t, "state", [&](const toml::table& e) {
            State x;
            r.named(x, e);
            reg.states.push_back(std::move(x));
        });
    }
    if (auto t = load("effects.toml")) {
        Reader r{file_of("effects.toml"), diags};
        r.each(*t, "effect", [&](const toml::table& e) {
            Effect x;
            r.named(x, e);
            x.tier = static_cast<int>(r.number(e, "tier").value_or(0));
            x.answer = r.boolean(e, "answer", false);
            reg.effects.push_back(std::move(x));
        });
    }
    if (auto t = load("origins.toml")) {
        Reader r{file_of("origins.toml"), diags};
        r.each(*t, "origin", [&](const toml::table& e) {
            Origin x;
            r.named(x, e);
            reg.origins.push_back(std::move(x));
        });
    }
    if (auto t = load("actions.toml")) {
        Reader r{file_of("actions.toml"), diags};
        r.each(*t, "action", [&](const toml::table& e) {
            Action x;
            r.named(x, e);
            x.effect = r.str(e, "effect", true);
            x.args = r.args(e, "args");
            reg.actions.push_back(std::move(x));
        });
    }
    if (auto t = load("extensions.toml")) {
        Reader r{file_of("extensions.toml"), diags};
        r.each(*t, "extension", [&](const toml::table& e) {
            Extension x;
            r.named(x, e);
            x.version = static_cast<int>(r.number(e, "version").value_or(1));
            x.requires_ = r.strings(e, "requires");
            reg.extensions.push_back(std::move(x));
        });
    }
    if (auto t = load("profiles.toml")) {
        Reader r{file_of("profiles.toml"), diags};
        r.each(*t, "profile", [&](const toml::table& e) {
            Profile x;
            r.named(x, e);
            x.scripts = r.strings(e, "scripts");
            x.min_scripts = static_cast<int>(r.number(e, "min_scripts").value_or(static_cast<double>(x.scripts.size())));
            reg.profiles.push_back(std::move(x));
        });
    }
    if (auto t = load("motion.toml")) {
        Reader r{file_of("motion.toml"), diags};
        reg.easing = r.string_map(*t, "easing");
        reg.spring = r.string_map(*t, "spring");
        r.each(*t, "token", [&](const toml::table& e) {
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
    }
    if (auto t = load("expr.toml")) {
        Reader r{file_of("expr.toml"), diags};
        reg.expr_types = r.strings(*t, "types");
        r.each(*t, "operator", [&](const toml::table& e) {
            ExprOperator x;
            r.named(x, e);
            x.text = r.str(e, "text", true);
            x.arity = static_cast<int>(r.number(e, "arity").value_or(0));
            x.combinators = r.string_map(e, "combinators");
            reg.operators.push_back(std::move(x));
        });
        r.each(*t, "function", [&](const toml::table& e) {
            ExprFunction x;
            r.named(x, e);
            x.signature = r.str(e, "signature", true);
            x.combinators = r.string_map(e, "combinators");
            reg.functions.push_back(std::move(x));
        });
    }
    if (auto t = load("abi.toml")) {
        Reader r{file_of("abi.toml"), diags};
        read_abi(*t, r, reg.abi);
    }

    unique_names(reg.roles, "role", diags);
    unique_names(reg.states, "state", diags);
    unique_names(reg.effects, "effect", diags);
    unique_names(reg.origins, "origin", diags);
    unique_names(reg.actions, "action", diags);
    unique_names(reg.extensions, "extension", diags);
    unique_names(reg.motion_tokens, "motion token", diags);
    if (reg.states.size() > 32) diags.error({file_of("states.toml"), 1, 1}, "more than 32 states do not fit the state set");
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
            if (utf8(entry.path().filename()).ends_with(".contract.omui.toml")) files.push_back(entry.path());
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
    Reader r{generic_utf8(file), diags};
    const toml::table* contracts = (*parsed)["contract"].as_table();
    if (!contracts) {
        diags.error(r.loc(&*parsed), "no [contract.<name>] table", "a contract file declares one or more [contract.<name>]");
        return out;
    }
    for (auto&& [key, node] : *contracts) {
        const toml::table* t = node.as_table();
        if (!t) continue;
        Contract c;
        c.name = std::string(key.str());
        c.loc = r.loc(t);
        c.doc = r.str(*t, "doc");
        c.role = r.str(*t, "role", true);
        c.spec = r.str(*t, "spec", true);
        c.children = r.str(*t, "children");
        if (c.children.empty()) c.children = "none";
        if (c.children != "none" && c.children != "any" && !valid_identifier(c.children))
            diags.error(r.loc(t->get("children")), std::format("`children` is none, any, or a contract name, not `{}`", c.children));
        if (!valid_identifier(c.name))
            diags.error(c.loc, std::format("contract name `{}` is not lower_snake_case", c.name));
        if (!c.role.empty() && !Registry::find(reg.roles, c.role))
            diags.error(r.loc(t->get("role")), std::format("unknown role `{}`", c.role), "see spec/registry/roles.toml");

        if (const toml::table* props = (*t)["props"].as_table()) {
            for (auto&& [pk, pv] : *props) {
                const toml::table* pt = pv.as_table();
                Prop p;
                p.name = std::string(pk.str());
                p.loc = r.loc(&pv);
                if (!pt) {
                    diags.error(p.loc, std::format("property `{}` must be a table {{ type = …, default = … }}", p.name));
                    continue;
                }
                p.type = r.str(*pt, "type", true);
                p.doc = r.str(*pt, "doc");
                p.values = r.strings(*pt, "values");
                p.bindable = r.boolean(*pt, "bindable", false);
                if (!valid_identifier(p.name)) diags.error(p.loc, std::format("property name `{}` is not lower_snake_case", p.name));
                if (std::ranges::find(prop_types, p.type) == prop_types.end())
                    diags.error(p.loc, std::format("property `{}` has unknown type `{}`", p.name, p.type),
                                "types: bool i32 i64 f64 string color enum list<string> any");
                if (p.type == "enum" && p.values.empty())
                    diags.error(p.loc, std::format("enum property `{}` lists no values", p.name));
                if (const toml::node* d = pt->get("default")) {
                    if (auto b = d->value_exact<bool>()) p.default_value = *b;
                    else if (auto i = d->value_exact<std::int64_t>()) p.default_value = *i;
                    else if (auto f = d->value_exact<double>()) p.default_value = *f;
                    else if (auto s = d->value_exact<std::string>()) p.default_value = *s;
                    else if (const toml::array* a = d->as_array()) {
                        std::vector<std::string> v;
                        for (const toml::node& e : *a)
                            if (auto s2 = e.value<std::string>()) v.push_back(*s2);
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

        if (const toml::table* events = (*t)["events"].as_table()) {
            for (auto&& [ek, ev] : *events) {
                Event e;
                e.name = std::string(ek.str());
                e.loc = r.loc(&ev);
                if (const toml::table* et = ev.as_table()) {
                    e.args = r.args(*et, "args");
                    e.coalesce = r.str(*et, "coalesce");
                    e.doc = r.str(*et, "doc");
                    if (!e.coalesce.empty() && e.coalesce != "frame")
                        diags.error(e.loc, std::format("event `{}`: coalesce must be \"frame\"", e.name));
                }
                for (const Arg& a : e.args)
                    if (!valid_arg_type(a.type))
                        diags.error(e.loc, std::format("event `{}` argument `{}` has unknown type `{}`", e.name, a.name, a.type));
                c.events.push_back(std::move(e));
            }
        }

        if (const toml::table* actions = (*t)["actions"].as_table()) {
            for (auto&& [ak, av] : *actions) {
                ContractAction a;
                a.name = std::string(ak.str());
                a.loc = r.loc(&av);
                if (const toml::table* at = av.as_table()) {
                    a.effect = r.str(*at, "effect", true);
                    a.args = r.args(*at, "args");
                    for (const Arg& x : a.args)
                        if (!valid_arg_type(x.type))
                            diags.error(a.loc, std::format("action `{}` argument `{}` has unknown type `{}`", a.name, x.name, x.type),
                                        "a type (bool i32 i64 f64 string point rect size any …), or alternatives: `f64 | min | max`");
                    a.doc = r.str(*at, "doc");
                    // `answer = "yes"` names the answer; `answer = true` means the usual "yes".
                    if (const auto* b = (*at)["answer"].as_boolean()) a.answer = b->get() ? "yes" : "";
                    else a.answer = r.str(*at, "answer");
                }
                const Effect* eff = Registry::find(reg.effects, a.effect);
                if (!eff) diags.error(a.loc, std::format("action `{}` names unknown effect `{}`", a.name, a.effect));
                if (eff && eff->answer && a.answer.empty())
                    diags.error(a.loc, std::format("action `{}` is `{}` but states no answer (S-1)", a.name, a.effect),
                                "add answer = \"yes\" (what an agent must say to confirm it)");
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
                                  "add doc = \"…\" so agents know what it does");
                }
                c.actions.push_back(std::move(a));
            }
        }

        if (const toml::table* commands = (*t)["commands"].as_table()) {
            for (auto&& [ck, cv] : *commands) {
                Command cmd;
                cmd.name = std::string(ck.str());
                cmd.loc = r.loc(&cv);
                if (const toml::table* ct = cv.as_table()) cmd.doc = r.str(*ct, "doc");
                c.commands.push_back(std::move(cmd));
            }
        }

        if (const toml::table* keys = (*t)["keys"].as_table()) {
            for (auto&& [kk, kv] : *keys) {
                Key k{std::string(kk.str()), kv.value<std::string>().value_or(""), r.loc(&kv)};
                std::string base = base_target(k.target);
                bool known = std::ranges::any_of(c.actions, [&](const ContractAction& a) { return a.name == base; }) ||
                             std::ranges::any_of(c.commands, [&](const Command& x) { return x.name == base; });
                if (!known)
                    diags.error(k.loc, std::format("key `{}` names `{}`, which is neither an action nor a command of `{}`",
                                                   k.chord, base, c.name),
                                std::format("declare it under [contract.{}.actions] or [contract.{}.commands]", c.name, c.name));
                c.keys.push_back(std::move(k));
            }
        }

        if (const toml::table* parts = (*t)["parts"].as_table()) {
            for (auto&& [pk, pv] : *parts) {
                Part p;
                p.name = std::string(pk.str());
                p.loc = r.loc(&pv);
                c.parts.push_back(std::move(p));
            }
        }

        if (const toml::table* motion = (*t)["motion"].as_table()) {
            for (auto&& [mk, mv] : *motion) {
                Motion m{std::string(mk.str()), mv.value<std::string>().value_or(""), r.loc(&mv)};
                if (m.token != "decay" && !Registry::find(reg.motion_tokens, m.token))
                    diags.error(m.loc, std::format("motion of `{}` names unknown token `{}`", m.part, m.token),
                                "see the [[token]] entries of spec/registry/motion.toml");
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

// `name = "type"` or `name = { type = "…", doc = "…" }`, in the order the
// file writes them (a TOML table keeps no order; the source does).
std::vector<NativeParam> native_params(const Reader& r, const toml::table& t, std::string_view key) {
    std::vector<NativeParam> out;
    const toml::node* n = t.get(key);
    if (!n) return out;
    const toml::table* m = n->as_table();
    if (!m) {
        r.diags.error(r.loc(n), std::format("`{}` must be a table: name = \"type\", or name = {{ type = …, doc = … }}", key));
        return out;
    }
    for (auto&& [k, v] : *m) {
        NativeParam p;
        p.name = std::string(k.str());
        p.loc = r.loc(&v);
        if (auto s = v.value<std::string>()) {
            p.type = *s;
        } else if (const toml::table* pt = v.as_table()) {
            p.type = r.str(*pt, "type", true);
            p.doc = r.str(*pt, "doc");
        } else {
            r.diags.error(p.loc, std::format("`{}` is \"type\" or {{ type = …, doc = … }}", p.name));
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
    Reader r{generic_utf8(file), diags};
    const toml::table* natives = (*parsed)["native"].as_table();
    if (!natives) {
        diags.error(r.loc(&*parsed), "no [native.<name>] table", "a native contract declares one or more [native.<name>]");
        return out;
    }
    for (auto&& [key, node] : *natives) {
        const toml::table* t = node.as_table();
        if (!t) continue;
        NativeModule m;
        m.name = std::string(key.str());
        m.loc = r.loc(t);
        m.doc = r.str(*t, "doc");
        m.spec = r.str(*t, "spec", true);
        if (!valid_identifier(m.name)) diags.error(m.loc, std::format("native module name `{}` is not lower_snake_case", m.name));
        if (m.doc.empty()) diags.warning(m.loc, std::format("native module `{}` has no doc", m.name), "add doc = \"…\": scripts and agents read it");

        std::set<std::string, std::less<>> records;
        if (const toml::table* types = (*t)["types"].as_table())
            for (auto&& [tk, tv] : *types) records.emplace(tk.str());
        if (const toml::table* types = (*t)["types"].as_table()) {
            for (auto&& [tk, tv] : *types) {
                NativeType ty;
                ty.name = std::string(tk.str());
                ty.loc = r.loc(&tv);
                const toml::table* tt = tv.as_table();
                if (!tt) {
                    diags.error(ty.loc, std::format("type `{}` must be a table {{ fields = {{ … }} }}", ty.name));
                    continue;
                }
                if (!pascal_identifier(ty.name)) diags.error(ty.loc, std::format("type name `{}` is not PascalCase", ty.name));
                ty.doc = r.str(*tt, "doc");
                ty.fields = native_params(r, *tt, "fields");
                if (ty.fields.empty()) diags.error(ty.loc, std::format("type `{}` has no fields", ty.name));
                for (const NativeParam& f : ty.fields)
                    if (auto p = native_type_problem(f.type, records); !p.empty())
                        diags.error(f.loc, std::format("{}.{}: {}", ty.name, f.name, p), "bool i32 u32 i64 f64 string any, a type of the module, T[], T?");
                m.types.push_back(std::move(ty));
            }
        }
        if (const toml::table* functions = (*t)["functions"].as_table()) {
            for (auto&& [fk, fv] : *functions) {
                NativeFunction f;
                f.name = std::string(fk.str());
                f.loc = r.loc(&fv);
                const toml::table* ft = fv.as_table();
                if (!ft) {
                    diags.error(f.loc, std::format("function `{}` must be a table {{ args = …, returns = … }}", f.name));
                    continue;
                }
                if (!valid_identifier(f.name)) diags.error(f.loc, std::format("function name `{}` is not lower_snake_case", f.name));
                f.doc = r.str(*ft, "doc");
                if (f.doc.empty()) diags.warning(f.loc, std::format("function `{}` has no doc", f.name), "add doc = \"…\": scripts and agents read it");
                f.effect = r.str(*ft, "effect");
                if (f.effect.empty()) f.effect = "read";
                f.async = r.boolean(*ft, "async", false);
                f.params = native_params(r, *ft, "args");
                f.returns = r.str(*ft, "returns");
                const Effect* eff = Registry::find(reg.effects, f.effect);
                if (!eff)
                    diags.error(r.loc(ft->get("effect")), std::format("function `{}` names unknown effect `{}`", f.name, f.effect));
                else if (eff->answer)
                    diags.error(r.loc(ft->get("effect")), std::format("function `{}` is `{}`, which needs an answer (S-1)", f.name, f.effect),
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
                        diags.error(r.loc(ft->get("returns")), std::format("{} returns: {}", f.name, x));
                m.functions.push_back(std::move(f));
            }
        }
        if (m.functions.empty()) diags.error(m.loc, std::format("native module `{}` declares no function", m.name));
        // The file's order, which a TOML table does not keep.
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
