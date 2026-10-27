// The `omui` Lua module. Lua is C: an error unwinds with longjmp, which skips
// C++ destructors, so no function here raises while a C++ object is alive:
// each entry point does its work in a scope that ends before it reports an
// error (`entry` below), and every call back into Lua is protected (pcall).
module;

#include <lua.hpp>

module openmui.lang.lua;

import std;
import nlohmann.json;
import openmui.abi;
import openmui.lang;

namespace omui::lang::lua {

namespace {

constexpr const char* signal_type = "omui.Signal";
constexpr const char* expr_type = "omui.Expr";
constexpr const char* element_type = "omui.Element";
constexpr const char* node_type = "omui.Node";
constexpr const char* model_type = "omui.Model";
constexpr const char* tr_type = "omui.Tr";

struct SignalBox {
    abi::OmuiSignal handle;
};
struct ExprBox {
    Expr expr;
};
struct ElementBox {
    Element element;
};
struct NodeBox {
    abi::OmuiNode node;
};
struct ModelBox {
    std::string name;
};
struct TrBox {
    std::string       key;
    std::vector<Expr> args;
};

template <class T>
T* make(lua_State* L, const char* meta, T value) {
    void* p = lua_newuserdatauv(L, sizeof(T), 0);
    T* t = new (p) T(std::move(value));
    luaL_setmetatable(L, meta);
    return t;
}

template <class T>
T* as(lua_State* L, int index, const char* meta) {
    return static_cast<T*>(luaL_testudata(L, index, meta));
}

template <class T>
int destroy(lua_State* L) {
    // __gc is called on the metatable's own type.
    static_cast<T*>(lua_touserdata(L, 1))->~T();
    return 0;
}

}  // namespace

struct Script::Impl {
    Session&                               session;
    lua_State*                             L = nullptr;
    std::function<void(std::string_view)>  errors = [](std::string_view m) { std::println(std::cerr, "openmui lua: {}", m); };
    std::optional<Json>                    catalog;  // components: which properties bind two-way
    std::vector<abi::OmuiSignal>           ordered;  // omui.signal's results, in order (kept across reloads)
    std::size_t                            next_signal = 0;
    std::map<std::string, abi::OmuiSignal> keyed;
    std::vector<int>                       refs;     // Lua functions the runtime may call (handlers, verbs)
    int                                    depth = 0;  // nested omui.each, for the rows' names

    explicit Impl(Session& s) : session(s) {}

    bool bindable(std::string_view type, std::string_view prop) {
        if (!catalog) {
            std::string out;
            session.host().ui([&] {
                (void)abi::omuiCatalog(
                    session.host().runtime(), [](void* u, const char* d, std::size_t n) { static_cast<std::string*>(u)->append(d, n); }, &out);
            });
            catalog = Json::parse(out, nullptr, false);
        }
        const Json* c = catalog->is_object() && catalog->contains("components") ? &(*catalog)["components"] : nullptr;
        const std::string name = contract_name(type);
        if (!c || !c->contains(name)) return false;
        const Json& props = (*c)[name]["props"];
        return props.contains(prop) && props[std::string(prop)].value("bindable", false);
    }
};

namespace {

Script::Impl& impl(lua_State* L) { return *static_cast<Script::Impl*>(lua_touserdata(L, lua_upvalueindex(1))); }

// An entry point: `body` answers its results' count, or -1 with an error
// message on the stack; the error is raised here, where nothing is alive.
template <int (*body)(lua_State*, Script::Impl&)>
int entry(lua_State* L) {
    const int n = body(L, impl(L));
    if (n < 0) return lua_error(L);
    return n;
}

int raise(lua_State* L, std::string_view message) {
    lua_pushlstring(L, message.data(), message.size());
    return -1;
}

// nil and a message: what a call that fails answers.
int failed(lua_State* L, std::string_view message) {
    lua_pushnil(L);
    lua_pushlstring(L, message.data(), message.size());
    return 2;
}

// ── values ──

bool is_array(lua_State* L, int index) {
    const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, index));
    lua_Integer count = 0;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        lua_pop(L, 1);
        ++count;
        if (lua_type(L, -1) != LUA_TNUMBER || !lua_isinteger(L, -1)) {
            lua_pop(L, 1);
            return false;
        }
    }
    return count == n && (n > 0 || count == 0);
}

std::optional<Json> to_json(lua_State* L, int index, int depth = 0) {
    index = lua_absindex(L, index);
    if (depth > 100) return std::nullopt;
    switch (lua_type(L, index)) {
        case LUA_TNIL: return Json(nullptr);
        case LUA_TBOOLEAN: return Json(lua_toboolean(L, index) != 0);
        case LUA_TNUMBER:
            if (lua_isinteger(L, index)) return Json(static_cast<std::int64_t>(lua_tointeger(L, index)));
            return Json(lua_tonumber(L, index));
        case LUA_TSTRING: {
            std::size_t n = 0;
            const char* s = lua_tolstring(L, index, &n);
            return Json(std::string(s, n));
        }
        case LUA_TLIGHTUSERDATA: return Json(nullptr);  // omui.null
        case LUA_TTABLE: {
            if (is_array(L, index)) {
                Json a = Json::array();
                const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, index));
                for (lua_Integer i = 1; i <= n; ++i) {
                    lua_rawgeti(L, index, i);
                    auto v = to_json(L, -1, depth + 1);
                    lua_pop(L, 1);
                    if (!v) return std::nullopt;
                    a.push_back(std::move(*v));
                }
                return a;
            }
            Json o = Json::object();
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                if (lua_type(L, -2) != LUA_TSTRING) {
                    lua_pop(L, 2);
                    return std::nullopt;
                }
                std::size_t n = 0;
                const char* k = lua_tolstring(L, -2, &n);
                auto v = to_json(L, -1, depth + 1);
                lua_pop(L, 1);
                if (!v) {
                    lua_pop(L, 1);
                    return std::nullopt;
                }
                o[std::string(k, n)] = std::move(*v);
            }
            return o;
        }
        default: return std::nullopt;
    }
}

void push_json(lua_State* L, const Json& v) {
    switch (v.type()) {
        case Json::value_t::null: lua_pushlightuserdata(L, nullptr); break;
        case Json::value_t::boolean: lua_pushboolean(L, v.get<bool>() ? 1 : 0); break;
        case Json::value_t::number_integer:
        case Json::value_t::number_unsigned: lua_pushinteger(L, v.get<lua_Integer>()); break;
        case Json::value_t::number_float: lua_pushnumber(L, v.get<double>()); break;
        case Json::value_t::string: {
            const auto& s = v.get_ref<const std::string&>();
            lua_pushlstring(L, s.data(), s.size());
            break;
        }
        case Json::value_t::array: {
            lua_createtable(L, static_cast<int>(v.size()), 0);
            lua_Integer i = 1;
            for (const Json& x : v) {
                push_json(L, x);
                lua_rawseti(L, -2, i++);
            }
            break;
        }
        case Json::value_t::object: {
            lua_createtable(L, 0, static_cast<int>(v.size()));
            for (auto it = v.begin(); it != v.end(); ++it) {
                push_json(L, it.value());
                lua_setfield(L, -2, it.key().c_str());
            }
            break;
        }
        default: lua_pushnil(L);
    }
}

// An operand of an expression: a signal, an expression, or a constant.
std::optional<Expr> to_expr(lua_State* L, int index) {
    if (auto* s = as<SignalBox>(L, index, signal_type)) return signal_ref(s->handle);
    if (auto* e = as<ExprBox>(L, index, expr_type)) return e->expr;
    auto v = to_json(L, index);
    if (!v) return std::nullopt;
    return literal(*v);
}

int push_expr(lua_State* L, Expr e) {
    make(L, expr_type, ExprBox{std::move(e)});
    return 1;
}

// A Lua function the runtime may call later, kept in the registry.
int keep_function(lua_State* L, Script::Impl& s, int index) {
    lua_pushvalue(L, index);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    s.refs.push_back(ref);
    return ref;
}

// Calls a kept function with one JSON argument, protected; an error goes to
// the script's error handler. Answers the first result as JSON.
std::optional<Json> call_kept(Script::Impl& s, int ref, const Json* arg) {
    lua_State* L = s.L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (arg) push_json(L, *arg);
    if (lua_pcall(L, arg ? 1 : 0, 1, 0) != LUA_OK) {
        std::size_t n = 0;
        const char* m = lua_tolstring(L, -1, &n);
        s.errors(std::string_view(m ? m : "error", m ? n : 5));
        lua_pop(L, 1);
        return std::nullopt;
    }
    auto out = to_json(L, -1);
    lua_pop(L, 1);
    return out ? out : std::optional<Json>(Json(nullptr));
}

// ── signals ──

int l_signal(lua_State* L, Script::Impl& s) {
    auto initial = to_json(L, 1);
    if (!initial) return raise(L, "omui.signal: the value is not data (nil, booleans, numbers, strings, tables)");
    std::string key;
    if (lua_type(L, 2) == LUA_TSTRING) key = lua_tostring(L, 2);
    abi::OmuiSignal h = 0;
    if (!key.empty()) {
        if (auto it = s.keyed.find(key); it != s.keyed.end()) h = it->second;
    } else if (s.next_signal < s.ordered.size()) {
        h = s.ordered[s.next_signal];  // a reload: the same signal, its value kept
    }
    if (!h) {
        auto made = s.session.signal(*initial);
        if (!made) return failed(L, made.error().text());
        h = *made;
        if (!key.empty()) s.keyed.emplace(key, h);
        else s.ordered.push_back(h);
    }
    if (key.empty()) ++s.next_signal;
    make(L, signal_type, SignalBox{h});
    return 1;
}

int l_signal_get(lua_State* L, Script::Impl& s) {
    auto* b = as<SignalBox>(L, 1, signal_type);
    if (!b) return raise(L, "signal:get(): not a signal");
    push_json(L, s.session.get(b->handle));
    return 1;
}

int l_signal_set(lua_State* L, Script::Impl& s) {
    auto* b = as<SignalBox>(L, 1, signal_type);
    if (!b) return raise(L, "signal:set(v): not a signal");
    auto v = to_json(L, 2);
    if (!v) return raise(L, "signal:set(v): the value is not data");
    s.session.set(b->handle, std::move(*v));
    return 0;
}

int l_signal_update(lua_State* L, Script::Impl& s) {
    auto* b = as<SignalBox>(L, 1, signal_type);
    if (!b || lua_type(L, 2) != LUA_TFUNCTION) return raise(L, "signal:update(fn): fn gets the value and answers the next");
    lua_pushvalue(L, 2);
    push_json(L, s.session.get(b->handle));
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) return -1;  // the message is on the stack
    auto v = to_json(L, -1);
    lua_pop(L, 1);
    if (!v) return raise(L, "signal:update(fn): fn answered something that is not data");
    s.session.set(b->handle, std::move(*v));
    return 0;
}

int l_signal_watch(lua_State* L, Script::Impl& s) {
    auto* b = as<SignalBox>(L, 1, signal_type);
    if (!b || lua_type(L, 2) != LUA_TFUNCTION) return raise(L, "signal:watch(fn): fn gets each new value");
    const int ref = keep_function(L, s, 2);
    Script::Impl* self = &s;
    s.session.watch(b->handle, [self, ref](const Json& v) { (void)call_kept(*self, ref, &v); });
    return 0;
}

// ── expressions ──

int l_binary(lua_State* L, std::string_view op) {
    auto a = to_expr(L, 1);
    auto b = to_expr(L, 2);
    if (!a || !b) return raise(L, std::format("`{}`: an operand is not a signal, an expression or data", op));
    return push_expr(L, binary(op, std::move(*a), std::move(*b)));
}

template <char... op>
int l_op(lua_State* L, Script::Impl&) {
    static constexpr char text[] = {op..., '\0'};
    return l_binary(L, text);
}

int l_unm(lua_State* L, Script::Impl&) {
    auto a = to_expr(L, 1);
    if (!a) return raise(L, "`-`: not an expression");
    return push_expr(L, unary("-", std::move(*a)));
}

int l_fmt(lua_State* L, Script::Impl&) {
    if (lua_type(L, 1) != LUA_TSTRING) return raise(L, "omui.fmt(pattern, …): the pattern is a string with {} for each value");
    const std::string pattern = lua_tostring(L, 1);
    std::vector<Expr> args;
    for (int i = 2; i <= lua_gettop(L); ++i) {
        auto e = to_expr(L, i);
        if (!e) return raise(L, std::format("omui.fmt: argument {} is not a signal, an expression or data", i));
        args.push_back(std::move(*e));
    }
    return push_expr(L, fmt(pattern, std::move(args)));
}

int l_call(lua_State* L, Script::Impl&) {
    if (lua_type(L, 1) != LUA_TSTRING) return raise(L, "omui.call(function, …): a function of the expression language (size, upper, clamp …)");
    const std::string f = lua_tostring(L, 1);
    std::vector<Expr> args;
    for (int i = 2; i <= lua_gettop(L); ++i) {
        auto e = to_expr(L, i);
        if (!e) return raise(L, std::format("omui.call: argument {} is not a signal, an expression or data", i));
        args.push_back(std::move(*e));
    }
    return push_expr(L, call(f, std::move(args)));
}

int l_select(lua_State* L, Script::Impl&) {
    auto c = to_expr(L, 1), a = to_expr(L, 2), b = to_expr(L, 3);
    if (!c || !a || !b) return raise(L, "omui.select(cond, then, else)");
    return push_expr(L, select(std::move(*c), std::move(*a), std::move(*b)));
}

int l_not(lua_State* L, Script::Impl&) {
    auto a = to_expr(L, 1);
    if (!a) return raise(L, "omui.not_(x)");
    return push_expr(L, unary("!", std::move(*a)));
}

template <char... op>
int l_compare(lua_State* L, Script::Impl&) {
    static constexpr char text[] = {op..., '\0'};
    return l_binary(L, text);
}

int l_and(lua_State* L, Script::Impl&) { return l_binary(L, "&&"); }
int l_or(lua_State* L, Script::Impl&) { return l_binary(L, "||"); }

int l_expr_index(lua_State* L, Script::Impl&) {
    // row.title, row.done: a field of a row (or of any map expression).
    auto* e = as<ExprBox>(L, 1, expr_type);
    if (!e || lua_type(L, 2) != LUA_TSTRING) return raise(L, "an expression's field is named by a string");
    return push_expr(L, member(e->expr, lua_tostring(L, 2)));
}

int l_tostring(lua_State* L, Script::Impl&) {
    if (auto* e = as<ExprBox>(L, 1, expr_type)) lua_pushfstring(L, "omui.Expr(%s)", e->expr.text.c_str());
    else if (auto* s = as<SignalBox>(L, 1, signal_type)) lua_pushfstring(L, "omui.Signal(s%d)", static_cast<int>(s->handle));
    else lua_pushstring(L, "omui value");
    return 1;
}

int l_tr(lua_State* L, Script::Impl&) {
    if (lua_type(L, 1) != LUA_TSTRING) return raise(L, "omui.tr(key, …): a message key");
    TrBox t{lua_tostring(L, 1), {}};
    for (int i = 2; i <= lua_gettop(L); ++i) {
        auto e = to_expr(L, i);
        if (!e) return raise(L, "omui.tr: an argument is not a signal, an expression or data");
        t.args.push_back(std::move(*e));
    }
    make(L, tr_type, std::move(t));
    return 1;
}

// ── elements ──

std::optional<std::string> fill(lua_State* L, Script::Impl& s, Element& e, int table) {
    table = lua_absindex(L, table);
    // Children: the array part, in order.
    const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, table));
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, table, i);
        auto* c = as<ElementBox>(L, -1, element_type);
        if (c) e.children.push_back(c->element);
        const bool ok = c != nullptr || lua_isnil(L, -1);
        lua_pop(L, 1);
        if (!ok) return std::format("{}: child {} is not an element (omui.Text {{…}}, omui.when …)", e.type, i);
    }
    lua_pushnil(L);
    while (lua_next(L, table) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 1);
            continue;
        }
        const std::string key = lua_tostring(L, -2);
        std::optional<std::string> err;
        if (key == "id") {
            if (lua_type(L, -1) != LUA_TSTRING) err = std::format("{}: an id is a string (spec S-8)", e.type);
            else e.id = lua_tostring(L, -1);
        } else if (key.starts_with("on_") && lua_type(L, -1) == LUA_TFUNCTION) {
            const int ref = keep_function(L, s, -1);
            Script::Impl* self = &s;
            e.on.emplace_back(key.substr(3), [self, ref](const Json& event) { (void)call_kept(*self, ref, &event); });
        } else if (key == "animate") {
            auto t = to_json(L, -1);
            if (!t || !t->is_object()) err = std::format("{}: animate is a table: property → transition", e.type);
            else
                for (auto it = t->begin(); it != t->end(); ++it) e.animate.emplace_back(it.key(), it.value());
        } else if (auto* sig = as<SignalBox>(L, -1, signal_type)) {
            // A signal: two-way where the component's property binds, one-way elsewhere.
            if (s.bindable(e.type, key)) e.props.emplace_back(key, Prop{.kind = Prop::Kind::bind, .signal = sig->handle});
            else e.props.emplace_back(key, Prop{.kind = Prop::Kind::expr, .expr = signal_ref(sig->handle)});
        } else if (auto* x = as<ExprBox>(L, -1, expr_type)) {
            e.props.emplace_back(key, Prop{.kind = Prop::Kind::expr, .expr = x->expr});
        } else if (auto* t = as<TrBox>(L, -1, tr_type)) {
            e.props.emplace_back(key, Prop{.kind = Prop::Kind::tr, .value = t->key, .args = t->args});
        } else {
            auto v = to_json(L, -1);
            if (!v) err = std::format("{}: `{}` is not data, a signal or an expression", e.type, key);
            else e.props.emplace_back(key, Prop{.value = std::move(*v)});
        }
        lua_pop(L, 1);
        if (err) {
            lua_pop(L, 1);
            return err;
        }
    }
    // Lua tables have no order: props sorted, so a document is the same every run.
    std::ranges::sort(e.props, {}, &std::pair<std::string, Prop>::first);
    std::ranges::sort(e.on, {}, &std::pair<std::string, Handler>::first);
    return std::nullopt;
}

int l_build(lua_State* L, Script::Impl& s) {
    Element e;
    e.type = lua_tostring(L, lua_upvalueindex(2));
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TTABLE) {
        if (auto err = fill(L, s, e, 1)) return raise(L, *err);
    } else if (lua_gettop(L) >= 1) {
        return raise(L, std::format("omui.{} takes a table: {{ prop = value, child, … }}", e.type));
    }
    make(L, element_type, ElementBox{std::move(e)});
    return 1;
}

std::optional<std::vector<Element>> elements(lua_State* L, int index, std::string_view what) {
    std::vector<Element> out;
    if (lua_isnoneornil(L, index)) return out;
    if (auto* e = as<ElementBox>(L, index, element_type)) {
        out.push_back(e->element);
        return out;
    }
    if (lua_type(L, index) != LUA_TTABLE) return std::nullopt;
    const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, index));
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, index, i);
        auto* e = as<ElementBox>(L, -1, element_type);
        if (e) out.push_back(e->element);
        lua_pop(L, 1);
        if (!e) return std::nullopt;
    }
    (void)what;
    return out;
}

int l_when(lua_State* L, Script::Impl&) {
    auto cond = to_expr(L, 1);
    auto then = elements(L, 2, "then");
    auto otherwise = elements(L, 3, "else");
    if (!cond || !then || !otherwise) return raise(L, "omui.when(cond, {elements…}, {elements…})");
    Element e;
    e.region = std::make_shared<Region>(Region{.kind = Region::Kind::when, .condition = std::move(*cond), .then = std::move(*then),
                                               .otherwise = std::move(*otherwise)});
    make(L, element_type, ElementBox{std::move(e)});
    return 1;
}

int l_each(lua_State* L, Script::Impl& s) {
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TFUNCTION)
        return raise(L, "omui.each(model, function(row) return element end [, where or function(row) return where end])");
    const std::string model = lua_tostring(L, 1);
    const std::string variable = s.depth ? std::format("row{}", s.depth + 1) : std::string("row");
    // The body is built once, with the row as an expression: row.title is
    // `row.title`, evaluated for each row by the runtime.
    lua_pushvalue(L, 2);
    make(L, expr_type, ExprBox{name_ref(variable)});
    ++s.depth;
    const int status = lua_pcall(L, 1, 1, 0);
    --s.depth;
    if (status != LUA_OK) return -1;
    auto* body = as<ElementBox>(L, -1, element_type);
    if (!body) return raise(L, "omui.each: the function answers one element, the view of a row");
    Region r{.kind = Region::Kind::each, .then = {body->element}, .model = model, .variable = variable};
    lua_pop(L, 1);
    if (lua_type(L, 3) == LUA_TFUNCTION) {
        // A filter of the row's own fields: function(row) return omui.not_(row.done) end.
        lua_pushvalue(L, 3);
        make(L, expr_type, ExprBox{name_ref(variable)});
        ++s.depth;
        const int filtered = lua_pcall(L, 1, 1, 0);
        --s.depth;
        if (filtered != LUA_OK) return -1;
        auto where = to_expr(L, -1);
        lua_pop(L, 1);
        if (!where) return raise(L, "omui.each: `where` answers an expression of the row");
        r.condition = std::move(*where);
    } else if (!lua_isnoneornil(L, 3)) {
        auto where = to_expr(L, 3);
        if (!where) return raise(L, "omui.each: `where` is an expression, or a function of the row");
        r.condition = std::move(*where);
    }
    Element e;
    e.region = std::make_shared<Region>(std::move(r));
    make(L, element_type, ElementBox{std::move(e)});
    return 1;
}

int l_slot(lua_State* L, Script::Impl&) {
    if (lua_type(L, 1) != LUA_TSTRING) return raise(L, "omui.slot(name)");
    Element e;
    e.region = std::make_shared<Region>(Region{.kind = Region::Kind::slot, .slot = lua_tostring(L, 1)});
    make(L, element_type, ElementBox{std::move(e)});
    return 1;
}

// ── views, verbs, models, the tree ──

int l_mount(lua_State* L, Script::Impl& s) {
    auto* e = as<ElementBox>(L, 2, element_type);
    if (lua_type(L, 1) != LUA_TSTRING || !e) return raise(L, "omui.mount(slot, element)");
    auto r = s.session.mount(lua_tostring(L, 1), e->element);
    if (!r) return failed(L, r.error().text());
    lua_pushboolean(L, 1);
    return 1;
}

int l_unmount(lua_State* L, Script::Impl& s) {
    if (lua_type(L, 1) != LUA_TSTRING) return raise(L, "omui.unmount(slot)");
    auto r = s.session.unmount(lua_tostring(L, 1));
    if (!r) return failed(L, r.error().text());
    lua_pushboolean(L, 1);
    return 1;
}

int l_document(lua_State* L, Script::Impl& s) {
    auto* e = as<ElementBox>(L, 1, element_type);
    if (!e) return raise(L, "omui.document(element)");
    const std::string text = s.session.document(e->element).dump();
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

abi::OmuiEffect effect_of(std::string_view e) {
    if (e == "read") return abi::OMUI_EFFECT_READ;
    if (e == "irreversible") return abi::OMUI_EFFECT_IRREVERSIBLE;
    if (e == "external") return abi::OMUI_EFFECT_EXTERNAL;
    return abi::OMUI_EFFECT_REVERSIBLE;
}

int l_verb(lua_State* L, Script::Impl& s) {
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 3) != LUA_TFUNCTION)
        return raise(L, "omui.verb(name, {effect = …, answer = …, summary = …}, function(args) … end)");
    Session::VerbSpec spec{lua_tostring(L, 1), "", abi::OMUI_EFFECT_REVERSIBLE, ""};
    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_getfield(L, 2, "effect");
        if (lua_type(L, -1) == LUA_TSTRING) spec.effect = effect_of(lua_tostring(L, -1));
        lua_getfield(L, 2, "answer");
        if (lua_type(L, -1) == LUA_TSTRING) spec.answer = lua_tostring(L, -1);
        lua_getfield(L, 2, "summary");
        if (lua_type(L, -1) == LUA_TSTRING) spec.summary = lua_tostring(L, -1);
        lua_pop(L, 3);
    }
    const int ref = keep_function(L, s, 3);
    Script::Impl* self = &s;
    auto r = s.session.verb(std::move(spec), [self, ref](const Json& args) -> Result<Json> {
        auto out = call_kept(*self, ref, &args);
        if (!out) return std::unexpected(Error{abi::OMUI_ERROR_REFUSED, "the verb raised an error"});
        return std::move(*out);
    });
    if (!r) return failed(L, r.error().text());
    lua_pushboolean(L, 1);
    return 1;
}

// A bus answer's records: the data of the last `data` record, or nil and the
// error's message.
int answer(lua_State* L, const Result<std::vector<Json>>& r) {
    if (!r) return failed(L, r.error().text());
    const Json* data = nullptr;
    std::string error;
    for (const Json& rec : *r) {
        if (rec.value("kind", "") == "data" && rec.contains("data")) data = &rec["data"];
        if (rec.value("kind", "") == "error" && error.empty()) error = rec.value("code", "error") + ": " + rec.value("message", "");
    }
    if (!error.empty()) return failed(L, error);
    if (data) push_json(L, *data);
    else lua_pushboolean(L, 1);
    return 1;
}

int l_invoke(lua_State* L, Script::Impl& s) {
    if (lua_type(L, 1) != LUA_TSTRING) return raise(L, "omui.invoke(verb, args)");
    auto args = lua_isnoneornil(L, 2) ? std::optional<Json>(Json::object()) : to_json(L, 2);
    if (!args) return raise(L, "omui.invoke: the arguments are data");
    return answer(L, s.session.invoke(lua_tostring(L, 1), *args));
}

int l_act(lua_State* L, Script::Impl& s) {
    auto req = to_json(L, 1);
    if (!req || !req->is_object()) return raise(L, "omui.act { node = …, action = …, answer = … }");
    return answer(L, s.session.act(*req));
}

int l_snapshot(lua_State* L, Script::Impl& s) {
    auto q = lua_isnoneornil(L, 1) ? std::optional<Json>(Json::object()) : to_json(L, 1);
    if (!q || !q->is_object()) return raise(L, "omui.snapshot { root = …, depth = … }");
    return answer(L, s.session.snapshot(*q));
}

int l_find(lua_State* L, Script::Impl& s) {
    if (lua_type(L, 1) != LUA_TSTRING && lua_type(L, 1) != LUA_TTABLE) return raise(L, "omui.find(path or query)");
    std::string selector;
    if (lua_type(L, 1) == LUA_TSTRING) selector = lua_tostring(L, 1);
    else if (auto q = to_json(L, 1)) selector = q->dump();
    auto n = s.session.find(selector);
    if (!n) return failed(L, n.error().text());
    make(L, node_type, NodeBox{*n});
    return 1;
}

int l_node_get(lua_State* L, Script::Impl& s) {
    auto* n = as<NodeBox>(L, 1, node_type);
    if (!n || lua_type(L, 2) != LUA_TSTRING) return raise(L, "node:get(prop)");
    auto v = s.session.get(n->node, lua_tostring(L, 2));
    if (!v) return failed(L, v.error().text());
    push_json(L, *v);
    return 1;
}

int l_node_set(lua_State* L, Script::Impl& s) {
    auto* n = as<NodeBox>(L, 1, node_type);
    auto t = to_json(L, 2);
    if (!n || !t || !t->is_object()) return raise(L, "node:set { prop = value, … }");
    std::vector<std::pair<std::string, Json>> props;
    for (auto it = t->begin(); it != t->end(); ++it) props.emplace_back(it.key(), it.value());
    auto r = s.session.set(n->node, props);
    if (!r) return failed(L, r.error().text());
    lua_pushboolean(L, 1);
    return 1;
}

int l_model(lua_State* L, Script::Impl&) {
    if (lua_type(L, 1) != LUA_TSTRING) return raise(L, "omui.model(name)");
    make(L, model_type, ModelBox{lua_tostring(L, 1)});
    return 1;
}

int l_model_splice(lua_State* L, Script::Impl& s) {
    auto* m = as<ModelBox>(L, 1, model_type);
    if (!m || !lua_isinteger(L, 2) || !lua_isinteger(L, 3)) return raise(L, "model:splice(from, remove, rows)");
    auto rows = lua_isnoneornil(L, 4) ? std::optional<Json>(Json::array()) : to_json(L, 4);
    if (!rows || !rows->is_array()) return raise(L, "model:splice: rows are a list of { key = …, fields… }");
    // Lua counts from 1; the model from 0.
    const lua_Integer from = lua_tointeger(L, 2) - 1;
    if (from < 0) return raise(L, "model:splice: rows are counted from 1");
    auto r = s.session.splice(m->name, static_cast<std::size_t>(from), static_cast<std::size_t>(std::max<lua_Integer>(0, lua_tointeger(L, 3))), *rows);
    if (!r) return failed(L, r.error().text());
    lua_pushboolean(L, 1);
    return 1;
}

int l_model_push(lua_State* L, Script::Impl& s) {
    auto* m = as<ModelBox>(L, 1, model_type);
    auto rows = to_json(L, 2);
    if (!m || !rows || !rows->is_array()) return raise(L, "model:push(rows)");
    auto now = s.session.rows(m->name);
    const std::size_t size = now && now->is_array() ? now->size() : 0;
    auto r = s.session.splice(m->name, size, 0, *rows);
    if (!r) return failed(L, r.error().text());
    lua_pushboolean(L, 1);
    return 1;
}

int l_model_rows(lua_State* L, Script::Impl& s) {
    auto* m = as<ModelBox>(L, 1, model_type);
    if (!m) return raise(L, "model:rows()");
    auto r = s.session.rows(m->name);
    if (!r) return failed(L, r.error().text());
    push_json(L, *r);
    return 1;
}

int l_log(lua_State* L, Script::Impl&) {
    std::string line;
    for (int i = 1; i <= lua_gettop(L); ++i) {
        std::size_t n = 0;
        const char* t = luaL_tolstring(L, i, &n);
        if (i > 1) line += ' ';
        line.append(t, n);
        lua_pop(L, 1);
    }
    std::println(std::cerr, "lua: {}", line);
    return 0;
}

// omui.<Component>: a builder for any registered component's name.
int l_module_index(lua_State* L, Script::Impl&) {
    if (lua_type(L, 2) != LUA_TSTRING) return 0;
    const std::string name = lua_tostring(L, 2);
    if (name.empty() || !(std::isupper(static_cast<unsigned char>(name[0])) || name.find('_') != std::string::npos)) return 0;
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushstring(L, name.c_str());
    lua_pushcclosure(L, entry<l_build>, 2);
    lua_pushvalue(L, -1);
    lua_setfield(L, 1, name.c_str());  // cached
    return 1;
}

void set_function(lua_State* L, Script::Impl& s, const char* name, lua_CFunction f) {
    lua_pushlightuserdata(L, &s);
    lua_pushcclosure(L, f, 1);
    lua_setfield(L, -2, name);
}

void metatable(lua_State* L, Script::Impl& s, const char* type, lua_CFunction gc, std::initializer_list<std::pair<const char*, lua_CFunction>> methods,
               std::initializer_list<std::pair<const char*, lua_CFunction>> meta = {}) {
    luaL_newmetatable(L, type);
    lua_pushcfunction(L, gc);
    lua_setfield(L, -2, "__gc");
    if (methods.size()) {
        lua_newtable(L);
        for (const auto& [n, f] : methods) set_function(L, s, n, f);
        lua_setfield(L, -2, "__index");
    }
    for (const auto& [n, f] : meta) set_function(L, s, n, f);
    lua_pop(L, 1);
}

int open_module(lua_State* L) {
    auto& s = *static_cast<Script::Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
    const std::initializer_list<std::pair<const char*, lua_CFunction>> arithmetic{
        {"__add", entry<l_op<'+'>>}, {"__sub", entry<l_op<'-'>>}, {"__mul", entry<l_op<'*'>>}, {"__div", entry<l_op<'/'>>},
        {"__mod", entry<l_op<'%'>>}, {"__unm", entry<l_unm>},       {"__tostring", entry<l_tostring>}};
    metatable(L, s, signal_type, destroy<SignalBox>,
              {{"get", entry<l_signal_get>}, {"set", entry<l_signal_set>}, {"update", entry<l_signal_update>}, {"watch", entry<l_signal_watch>}},
              arithmetic);
    metatable(L, s, expr_type, destroy<ExprBox>, {}, arithmetic);
    // An expression's fields (row.title) come from __index, set after the arithmetic.
    luaL_getmetatable(L, expr_type);
    set_function(L, s, "__index", entry<l_expr_index>);
    lua_pop(L, 1);
    metatable(L, s, element_type, destroy<ElementBox>, {});
    metatable(L, s, node_type, destroy<NodeBox>, {{"get", entry<l_node_get>}, {"set", entry<l_node_set>}});
    metatable(L, s, model_type, destroy<ModelBox>, {{"splice", entry<l_model_splice>}, {"push", entry<l_model_push>}, {"rows", entry<l_model_rows>}});
    metatable(L, s, tr_type, destroy<TrBox>, {});

    lua_newtable(L);
    set_function(L, s, "signal", entry<l_signal>);
    set_function(L, s, "fmt", entry<l_fmt>);
    set_function(L, s, "call", entry<l_call>);
    set_function(L, s, "select", entry<l_select>);
    set_function(L, s, "not_", entry<l_not>);
    set_function(L, s, "eq", entry<l_compare<'=', '='>>);
    set_function(L, s, "ne", entry<l_compare<'!', '='>>);
    set_function(L, s, "lt", entry<l_compare<'<'>>);
    set_function(L, s, "le", entry<l_compare<'<', '='>>);
    set_function(L, s, "gt", entry<l_compare<'>'>>);
    set_function(L, s, "ge", entry<l_compare<'>', '='>>);
    set_function(L, s, "and_", entry<l_and>);
    set_function(L, s, "or_", entry<l_or>);
    set_function(L, s, "tr", entry<l_tr>);
    set_function(L, s, "when", entry<l_when>);
    set_function(L, s, "each", entry<l_each>);
    set_function(L, s, "slot", entry<l_slot>);
    set_function(L, s, "mount", entry<l_mount>);
    set_function(L, s, "unmount", entry<l_unmount>);
    set_function(L, s, "document", entry<l_document>);
    set_function(L, s, "verb", entry<l_verb>);
    set_function(L, s, "invoke", entry<l_invoke>);
    set_function(L, s, "act", entry<l_act>);
    set_function(L, s, "snapshot", entry<l_snapshot>);
    set_function(L, s, "find", entry<l_find>);
    set_function(L, s, "model", entry<l_model>);
    set_function(L, s, "log", entry<l_log>);
    lua_pushlightuserdata(L, nullptr);
    lua_setfield(L, -2, "null");
    // omui.Column, omui.Text …: builders, by name.
    lua_newtable(L);
    set_function(L, s, "__index", entry<l_module_index>);
    lua_setmetatable(L, -2);
    return 1;
}

}  // namespace

Script::Script(Session& session) : impl_(std::make_unique<Impl>(session)) {
    Impl* s = impl_.get();
    session.host().script([s] {
        s->L = luaL_newstate();
        luaL_openlibs(s->L);
        lua_pushlightuserdata(s->L, s);
        lua_pushcclosure(s->L, open_module, 1);
        lua_pushvalue(s->L, -1);
        lua_call(s->L, 0, 1);
        lua_setglobal(s->L, "omui");
        // require "openmui" answers the same module.
        lua_getglobal(s->L, "package");
        lua_getfield(s->L, -1, "preload");
        lua_pushvalue(s->L, -3);
        lua_setfield(s->L, -2, "openmui");
        lua_pop(s->L, 3);
    });
}

Script::~Script() {
    Impl* s = impl_.get();
    s->session.host().script([s] {
        if (s->L) lua_close(s->L);
        s->L = nullptr;
    });
}

void Script::on_error(std::function<void(std::string_view)> fn) { impl_->errors = std::move(fn); }

namespace {

Result<void> run_chunk(Script::Impl& s, const std::string& code, const std::string& name) {
    lua_State* L = s.L;
    if (luaL_loadbufferx(L, code.data(), code.size(), name.c_str(), "t") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::size_t n = 0;
        const char* m = lua_tolstring(L, -1, &n);
        std::string message(m ? m : "error", m ? n : 5);
        lua_pop(L, 1);
        return std::unexpected(Error{abi::OMUI_ERROR_INVALID_ARGUMENT, std::move(message)});
    }
    return {};
}

}  // namespace

std::future<Result<void>> Script::run(std::string code, std::string name) {
    auto done = std::make_shared<std::promise<Result<void>>>();
    auto future = done->get_future();
    Impl* s = impl_.get();
    s->session.host().post([s, done, code = std::move(code), name = std::move(name)] { done->set_value(run_chunk(*s, code, name)); });
    return future;
}

std::future<Result<void>> Script::reload(std::string code, std::string name) {
    auto done = std::make_shared<std::promise<Result<void>>>();
    auto future = done->get_future();
    Impl* s = impl_.get();
    s->session.host().post([s, done, code = std::move(code), name = std::move(name)] {
        s->next_signal = 0;  // the chunk's signals are the last run's, in order
        done->set_value(run_chunk(*s, code, name));
    });
    return future;
}

}  // namespace omui::lang::lua
