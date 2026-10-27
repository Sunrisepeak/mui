// openmui.native — the native modules of a program (component-binding §8).
//
//     # native/fuzzy.native.omui.toml
//     [native.fuzzy.functions.rank]
//     args    = { query = "string", candidates = "string[]", limit = "u32" }
//     returns = "Match[]"
//
// The C++ that implements a contract is registered once, by the `define()`
// that `omui-gen native` generates; scripts call it through the C ABI
// (omuiNativeCatalog, omuiNativeCall), and their bindings are made from the
// catalogue, never by hand. Arguments are checked against the contract's
// types here, so a call reaches the implementation only with values of its
// declared types.
//
// Types: bool, i32, u32, i64, f64, string, any (JSON), a record the module
// declares (PascalCase), `T[]` (a list) and `T?` (optional: absent or null).
export module openmui.native;

import std;
import nlohmann.json;

export namespace omui::native {

using Json = nlohmann::json;

// Why a call did not answer: `invalid-argument` (the arguments are not the
// contract's), `not-found` (no such module or function), `failed` (the
// implementation said why).
struct Failure {
    std::string code;
    std::string message;
};

using Result = std::expected<Json, Failure>;

struct Param {
    std::string name;
    std::string type;  // as the contract writes it: "string[]", "Match?"
    std::string doc;
};

struct Function {
    std::string        name;
    std::string        doc;
    std::string        effect = "read";  // the registry's effects (spec §3.3)
    bool               async = false;    // scripts may await it; it runs on a worker thread
    std::vector<Param> params;           // in the contract's order (positional calls)
    std::string        returns;          // empty: nothing
    // The implementation, given the arguments by name, already checked.
    std::function<Result(const Json& args)> call;
};

struct Type {
    std::string        name;
    std::string        doc;
    std::vector<Param> fields;
};

struct Module {
    std::string           name;
    std::string           doc;
    std::string           spec = "0.1";
    std::vector<Type>     types;
    std::vector<Function> functions;
};

// Registers a module. A name is registered once: a second definition of it
// is refused (two implementations of one contract would be ambiguous).
std::expected<void, std::string> define(Module module);

// A registered module, or nothing. Modules are never removed: the pointer
// stays valid for the life of the program.
[[nodiscard]] const Module* find(std::string_view name);
[[nodiscard]] std::vector<const Module*> modules();

// The registered modules, as bindings and agents read them:
//   {"modules": {name: {"doc", "spec", "types": {T: {"doc", "fields": [{name, type, doc}]}},
//                       "functions": {f: {"doc", "effect", "async", "params": [...], "returns"}}}}}
[[nodiscard]] Json catalog();

// Calls a function: the arguments a list (positional, in the contract's
// order) or an object (by name). They are checked against the contract
// first; then the implementation runs, on the calling thread.
[[nodiscard]] Result call(std::string_view module, std::string_view function, const Json& args);

// What is wrong with a value of a contract type (empty: nothing). `where`
// names the value in the message (`candidates[2]`).
[[nodiscard]] std::string check(const Module& module, const Json& value, std::string_view type, std::string_view where);

// ── conversions the generated code uses ────────────────────────────────────
// decode(json, std::type_identity<T>{}) and encode(value) for the contract's
// types; a module's records add their own overloads, found by ADL.

inline bool decode(const Json& j, std::type_identity<bool>) { return j.get<bool>(); }
inline std::int32_t decode(const Json& j, std::type_identity<std::int32_t>) { return j.get<std::int32_t>(); }
inline std::uint32_t decode(const Json& j, std::type_identity<std::uint32_t>) { return j.get<std::uint32_t>(); }
inline std::int64_t decode(const Json& j, std::type_identity<std::int64_t>) { return j.get<std::int64_t>(); }
inline double decode(const Json& j, std::type_identity<double>) { return j.get<double>(); }
inline std::string decode(const Json& j, std::type_identity<std::string>) { return j.get<std::string>(); }
inline Json decode(const Json& j, std::type_identity<Json>) { return j; }
template <class T>
std::vector<T> decode(const Json& j, std::type_identity<std::vector<T>>) {
    std::vector<T> out;
    out.reserve(j.size());
    for (const Json& x : j) out.push_back(decode(x, std::type_identity<T>{}));
    return out;
}
template <class T>
std::optional<T> decode(const Json& j, std::type_identity<std::optional<T>>) {
    if (j.is_null()) return std::nullopt;
    return decode(j, std::type_identity<T>{});
}

inline Json encode(bool v) { return Json(v); }
inline Json encode(std::int32_t v) { return Json(v); }
inline Json encode(std::uint32_t v) { return Json(v); }
inline Json encode(std::int64_t v) { return Json(v); }
inline Json encode(double v) { return Json(v); }
inline Json encode(const std::string& v) { return Json(v); }
inline Json encode(const Json& v) { return v; }
template <class T>
Json encode(const std::vector<T>& v) {
    Json out = Json::array();
    for (const T& x : v) out.push_back(encode(x));
    return out;
}
template <class T>
Json encode(const std::optional<T>& v) {
    return v ? encode(*v) : Json(nullptr);
}

}  // namespace omui::native
