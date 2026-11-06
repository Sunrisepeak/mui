module openmui.native;

import std;
import nlohmann.json;

namespace omui::native {

namespace {

struct Registry {
    std::mutex                          mutex;
    std::deque<Module>                  modules;  // a deque: registered modules never move
    std::map<std::string, const Module*, std::less<>> by_name;
};

Registry& registry() {
    static Registry r;
    return r;
}

constexpr std::array<std::string_view, 7> scalars{"bool", "i32", "u32", "i64", "f64", "string", "any"};

bool is_name(std::string_view s, bool upper_first) {
    if (s.empty()) return false;
    const char c = s.front();
    if (upper_first ? !(c >= 'A' && c <= 'Z') : !(c >= 'a' && c <= 'z')) return false;
    return std::ranges::all_of(s, [](char x) { return (x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || (x >= '0' && x <= '9') || x == '_'; });
}

const Type* record(const Module& m, std::string_view name) {
    for (const Type& t : m.types)
        if (t.name == name) return &t;
    return nullptr;
}

// What is wrong with a type as a contract writes it (empty: nothing).
std::string type_problem(const Module& m, std::string_view type) {
    if (type.ends_with("?")) return type_problem(m, type.substr(0, type.size() - 1));
    if (type.ends_with("[]")) return type_problem(m, type.substr(0, type.size() - 2));
    if (std::ranges::find(scalars, type) != scalars.end()) return {};
    if (record(m, type)) return {};
    return std::format("unknown type `{}` (bool i32 u32 i64 f64 string any, a record of the module, T[], T?)", type);
}

bool integral(const Json& v, double lo, double hi) {
    if (v.is_number_integer() || v.is_number_unsigned()) {
        const double d = v.is_number_unsigned() ? static_cast<double>(v.get<std::uint64_t>()) : static_cast<double>(v.get<std::int64_t>());
        return d >= lo && d <= hi;
    }
    if (!v.is_number_float()) return false;
    const double d = v.get<double>();
    return std::isfinite(d) && d == std::trunc(d) && d >= lo && d <= hi;
}

std::string kind_of(const Json& v) {
    if (v.is_null()) return "null";
    if (v.is_boolean()) return "a bool";
    if (v.is_number()) return std::format("the number {}", v.dump());
    if (v.is_string()) return "a string";
    if (v.is_array()) return "a list";
    return "an object";
}

}  // namespace

std::string check(const Module& m, const Json& v, std::string_view type, std::string_view where) {
    if (type.ends_with("?")) return v.is_null() ? std::string() : check(m, v, type.substr(0, type.size() - 1), where);
    auto wrong = [&](std::string_view what) { return std::format("`{}` is {}, not {}", where, kind_of(v), what); };
    if (type.ends_with("[]")) {
        if (!v.is_array()) return wrong(std::format("a list ({})", type));
        const std::string_view item = type.substr(0, type.size() - 2);
        for (std::size_t i = 0; i < v.size(); ++i)
            if (auto p = check(m, v[i], item, std::format("{}[{}]", where, i)); !p.empty()) return p;
        return {};
    }
    if (type == "any") return {};
    if (type == "bool") return v.is_boolean() ? std::string() : wrong("a bool");
    if (type == "string") return v.is_string() ? std::string() : wrong("a string");
    if (type == "f64") return v.is_number() ? std::string() : wrong("a number (f64)");
    if (type == "i32") return integral(v, -2147483648.0, 2147483647.0) ? std::string() : wrong("an integer (i32)");
    if (type == "u32") return integral(v, 0.0, 4294967295.0) ? std::string() : wrong("a non-negative integer (u32)");
    if (type == "i64") return integral(v, -9007199254740992.0, 9007199254740992.0) ? std::string() : wrong("an integer (i64)");
    const Type* t = record(m, type);
    if (!t) return std::format("`{}` has the unknown type `{}`", where, type);
    if (!v.is_object()) return wrong(std::format("an object ({})", type));
    for (const Param& f : t->fields) {
        const std::string at = std::format("{}.{}", where, f.name);
        if (!v.contains(f.name)) {
            if (f.type.ends_with("?")) continue;
            return std::format("`{}` ({}) is missing", at, f.type);
        }
        if (auto p = check(m, v[f.name], f.type, at); !p.empty()) return p;
    }
    for (auto it = v.begin(); it != v.end(); ++it)
        if (std::ranges::none_of(t->fields, [&](const Param& f) { return f.name == it.key(); }))
            return std::format("`{}.{}` is not a field of {}", where, it.key(), type);
    return {};
}

std::expected<void, std::string> define(Module m) {
    if (!is_name(m.name, false)) return std::unexpected(std::format("a native module's name is snake_case: `{}`", m.name));
    std::set<std::string, std::less<>> names;
    for (const Type& t : m.types) {
        if (!is_name(t.name, true)) return std::unexpected(std::format("{}: a type's name is PascalCase: `{}`", m.name, t.name));
        if (!names.insert(t.name).second) return std::unexpected(std::format("{}: the type `{}` is declared twice", m.name, t.name));
    }
    for (const Type& t : m.types)
        for (const Param& f : t.fields)
            if (auto p = type_problem(m, f.type); !p.empty()) return std::unexpected(std::format("{}.{}.{}: {}", m.name, t.name, f.name, p));
    names.clear();
    for (const Function& f : m.functions) {
        if (!is_name(f.name, false)) return std::unexpected(std::format("{}: a function's name is snake_case: `{}`", m.name, f.name));
        if (!names.insert(f.name).second) return std::unexpected(std::format("{}: the function `{}` is declared twice", m.name, f.name));
        if (!f.call) return std::unexpected(std::format("{}.{} has no implementation", m.name, f.name));
        std::set<std::string, std::less<>> params;
        for (const Param& p : f.params) {
            if (!params.insert(p.name).second) return std::unexpected(std::format("{}.{}: the argument `{}` is declared twice", m.name, f.name, p.name));
            if (auto x = type_problem(m, p.type); !x.empty()) return std::unexpected(std::format("{}.{}({}): {}", m.name, f.name, p.name, x));
        }
        if (!f.returns.empty())
            if (auto x = type_problem(m, f.returns); !x.empty()) return std::unexpected(std::format("{}.{} returns: {}", m.name, f.name, x));
    }
    Registry& r = registry();
    std::scoped_lock lock(r.mutex);
    if (r.by_name.contains(m.name)) return std::unexpected(std::format("the native module `{}` is already defined", m.name));
    r.modules.push_back(std::move(m));
    r.by_name.emplace(r.modules.back().name, &r.modules.back());
    return {};
}

const Module* find(std::string_view name) {
    Registry& r = registry();
    std::scoped_lock lock(r.mutex);
    auto it = r.by_name.find(name);
    return it == r.by_name.end() ? nullptr : it->second;
}

std::vector<const Module*> modules() {
    Registry& r = registry();
    std::scoped_lock lock(r.mutex);
    std::vector<const Module*> out;
    for (const auto& [name, m] : r.by_name) out.push_back(m);
    return out;
}

Json catalog() {
    auto params = [](const std::vector<Param>& ps) {
        Json out = Json::array();
        for (const Param& p : ps) {
            Json x{{"name", p.name}, {"type", p.type}};
            if (!p.doc.empty()) x["doc"] = p.doc;
            out.push_back(std::move(x));
        }
        return out;
    };
    Json out = Json::object();
    for (const Module* m : modules()) {
        Json types = Json::object();
        for (const Type& t : m->types) types[t.name] = Json{{"doc", t.doc}, {"fields", params(t.fields)}};
        Json functions = Json::object();
        for (const Function& f : m->functions) {
            Json x{{"doc", f.doc}, {"effect", f.effect}, {"async", f.async}, {"params", params(f.params)}};
            if (!f.returns.empty()) x["returns"] = f.returns;
            functions[f.name] = std::move(x);
        }
        out[m->name] = Json{{"doc", m->doc}, {"spec", m->spec}, {"types", std::move(types)}, {"functions", std::move(functions)}};
    }
    return Json{{"modules", std::move(out)}};
}

Result call(std::string_view module, std::string_view function, const Json& args) {
    const Module* m = find(module);
    if (!m) return std::unexpected(Failure{"not-found", std::format("no native module is named `{}`", module)});
    const Function* f = nullptr;
    for (const Function& x : m->functions)
        if (x.name == function) f = &x;
    if (!f) {
        std::string names;
        for (const Function& x : m->functions) names += (names.empty() ? "" : ", ") + x.name;
        return std::unexpected(Failure{"not-found", std::format("the native module `{}` has no function `{}` (it has {})", module, function, names)});
    }
    const std::string sig = [&] {
        std::string s;
        for (const Param& p : f->params) s += std::format("{}{}: {}", s.empty() ? "" : ", ", p.name, p.type);
        return std::format("{}.{}({})", module, function, s);
    }();
    auto invalid = [&](std::string message) { return std::unexpected(Failure{"invalid-argument", std::format("{}: {}", sig, message)}); };
    // By name, whichever way they came.
    Json named = Json::object();
    if (args.is_array()) {
        if (args.size() > f->params.size()) return invalid(std::format("{} arguments given, {} taken", args.size(), f->params.size()));
        for (std::size_t i = 0; i < args.size(); ++i) named[f->params[i].name] = args[i];
    } else if (args.is_object()) {
        for (auto it = args.begin(); it != args.end(); ++it)
            if (std::ranges::none_of(f->params, [&](const Param& p) { return p.name == it.key(); }))
                return invalid(std::format("`{}` is not an argument of it", it.key()));
        named = args;
    } else if (!args.is_null()) {
        return invalid("the arguments are a list, or an object by name");
    }
    for (const Param& p : f->params) {
        if (!named.contains(p.name) || named[p.name].is_null()) {
            if (p.type.ends_with("?")) {
                named[p.name] = nullptr;
                continue;
            }
            return invalid(std::format("`{}` ({}) is missing", p.name, p.type));
        }
        if (auto problem = check(*m, named[p.name], p.type, p.name); !problem.empty()) return invalid(problem);
    }
    // Checked: the conversions the generated code makes cannot fail. An
    // implementation says why it did not answer (std::expected), as `failed`.
    Result r = f->call(named);
    if (!r && r.error().message.find(sig) == std::string::npos) r.error().message = std::format("{}: {}", sig, r.error().message);
    return r;
}

}  // namespace omui::native
