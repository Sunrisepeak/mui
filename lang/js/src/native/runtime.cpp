// The `openmui` module's native half (`__omui`) and the QuickJS-ng runtime.
// Everything here runs on the script thread: QuickJS is single-threaded.
// The JavaScript functions the runtime may call later (handlers, verbs,
// watches) are kept in a table by id; what reaches the runtime is the id.
// QuickJS-ng's API is the module mcpplibs.mwrap.quickjs_ng (quickjs-ng-m):
// no header here.
module openmui.lang.js;

import std;
import mcpplibs.mwrap.quickjs_ng;
import nlohmann.json;
import openmui.abi;
import openmui.lang;
import :prelude;

namespace omui::lang::js {

using namespace mcpplibs::mwrap::quickjs_ng;          // quickjs.h's static inline API (JS_IsException, JS_ToCString …)
using namespace mcpplibs::mwrap::quickjs_ng::macros;  // its macros (JS_UNDEFINED, JS_EVAL_TYPE_MODULE …)

struct Script::Impl {
    Session&                                    session;
    JSRuntime*                                  rt = nullptr;
    JSContext*                                  ctx = nullptr;
    std::function<void(std::string_view)>       errors = [](std::string_view m) { std::println(std::cerr, "openmui js: {}", m); };
    std::string                                 module_source;  // the prelude and a builder per component
    std::vector<abi::OmuiSignal>                ordered;        // signal()'s results in order, kept across reloads
    std::size_t                                 next_signal = 0;
    std::map<std::string, abi::OmuiSignal>      keyed;
    std::map<int, JSValue>                      functions;
    int                                         next_function = 1;
    std::map<std::string, std::vector<int>>     slot_functions;  // a mounted view's handlers
    std::vector<int>                            pending_slot;    // handlers of the mount in progress
    std::shared_ptr<bool>                       alive = std::make_shared<bool>(true);  // false once the context is gone

    explicit Impl(Session& s) : session(s) {}

    int keep(JSValueConst f) {
        const int id = next_function++;
        functions.emplace(id, JS_DupValue(ctx, f));
        return id;
    }
    void release(int id) {
        if (auto it = functions.find(id); it != functions.end()) {
            JS_FreeValue(ctx, it->second);
            functions.erase(it);
        }
    }

    std::string exception_text() {
        JSValue e = JS_GetException(ctx);
        std::string out;
        if (const char* s = JS_ToCString(ctx, e)) {
            out = s;
            JS_FreeCString(ctx, s);
        }
        JSValue stack = JS_GetPropertyStr(ctx, e, "stack");
        if (!JS_IsUndefined(stack))
            if (const char* s = JS_ToCString(ctx, stack)) {
                if (*s) out += std::format("\n{}", s);
                JS_FreeCString(ctx, s);
            }
        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, e);
        return out;
    }

    // Promise jobs (async functions, then): run until none is left.
    void run_jobs() {
        JSContext* c = nullptr;
        while (true) {
            const int r = JS_ExecutePendingJob(rt, &c);
            if (r == 0) return;
            if (r < 0) errors(exception_text());
        }
    }

    // Calls a kept function with one argument (a JSON text, parsed or as a
    // string); answers its result as text (JSON.stringify'd by the prelude for
    // verbs), or nothing when it threw.
    std::optional<std::string> call(int id, std::string_view arg, bool parse) {
        auto it = functions.find(id);
        if (it == functions.end()) return std::nullopt;
        JSValue a = parse ? JS_ParseJSON(ctx, arg.data(), arg.size(), "<event>") : JS_NewStringLen(ctx, arg.data(), arg.size());
        JSValue r = JS_Call(ctx, it->second, JS_UNDEFINED, 1, &a);
        JS_FreeValue(ctx, a);
        std::optional<std::string> out;
        if (JS_IsException(r)) {
            errors(exception_text());
        } else {
            out.emplace();
            if (JS_IsString(r))
                if (const char* s = JS_ToCString(ctx, r)) {
                    *out = s;
                    JS_FreeCString(ctx, s);
                }
        }
        JS_FreeValue(ctx, r);
        run_jobs();
        return out;
    }
};

namespace {

Script::Impl& impl(JSContext* ctx) { return *static_cast<Script::Impl*>(JS_GetContextOpaque(ctx)); }

std::string text(JSContext* ctx, JSValueConst v) {
    std::size_t n = 0;
    const char* s = JS_ToCStringLen(ctx, &n, v);
    if (!s) return {};
    std::string out(s, n);
    JS_FreeCString(ctx, s);
    return out;
}

JSValue string(JSContext* ctx, std::string_view s) { return JS_NewStringLen(ctx, s.data(), s.size()); }

JSValue fail(JSContext* ctx, std::string_view message) { return JS_ThrowPlainError(ctx, "%.*s", static_cast<int>(message.size()), message.data()); }

std::int64_t integer(JSContext* ctx, JSValueConst v) {
    std::int64_t x = 0;
    (void)JS_ToInt64(ctx, &x, v);
    return x;
}

// ── __omui ──

JSValue n_signal_create(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto& s = impl(ctx);
    const Json initial = Json::parse(text(ctx, argv[0]), nullptr, false);
    const std::string key = argc > 1 ? text(ctx, argv[1]) : std::string();
    abi::OmuiSignal h = 0;
    if (!key.empty()) {
        if (auto it = s.keyed.find(key); it != s.keyed.end()) h = it->second;
    } else if (s.next_signal < s.ordered.size()) {
        h = s.ordered[s.next_signal];  // a reload: the same signal, its value kept
    }
    if (!h) {
        auto made = s.session.signal(initial);
        if (!made) return fail(ctx, made.error().text());
        h = *made;
        if (!key.empty()) s.keyed.emplace(key, h);
        else s.ordered.push_back(h);
    }
    if (key.empty()) ++s.next_signal;
    return JS_NewInt64(ctx, static_cast<std::int64_t>(h));
}

JSValue n_signal_get(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    return string(ctx, impl(ctx).session.get(static_cast<abi::OmuiSignal>(integer(ctx, argv[0]))).dump());
}

JSValue n_signal_set(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    Json v = Json::parse(text(ctx, argv[1]), nullptr, false);
    impl(ctx).session.set(static_cast<abi::OmuiSignal>(integer(ctx, argv[0])), std::move(v));
    return JS_UNDEFINED;
}

JSValue n_signal_watch(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto& s = impl(ctx);
    if (!JS_IsFunction(ctx, argv[1])) return fail(ctx, "watch(fn)");
    const int id = s.keep(argv[1]);
    Script::Impl* self = &s;
    s.session.watch(static_cast<abi::OmuiSignal>(integer(ctx, argv[0])), [self, id](const Json& v) { (void)self->call(id, v.dump(), false); });
    return JS_UNDEFINED;
}

JSValue n_catalog(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    std::string out;
    impl(ctx).session.host().ui([&] {
        (void)abi::omuiCatalog(impl(ctx).session.host().runtime(), [](void* u, const char* d, std::size_t n) { static_cast<std::string*>(u)->append(d, n); }, &out);
    });
    return string(ctx, out);
}

JSValue n_mount(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto& s = impl(ctx);
    const std::string slot = text(ctx, argv[0]);
    const Json tree = Json::parse(text(ctx, argv[1]), nullptr, false);
    std::vector<int> handlers;
    const std::int64_t count = [&] {
        JSValue len = JS_GetPropertyStr(ctx, argv[2], "length");
        const std::int64_t n = integer(ctx, len);
        JS_FreeValue(ctx, len);
        return n;
    }();
    for (std::int64_t i = 0; i < count; ++i) {
        JSValue f = JS_GetPropertyUint32(ctx, argv[2], static_cast<std::uint32_t>(i));
        handlers.push_back(s.keep(f));
        JS_FreeValue(ctx, f);
    }
    Script::Impl* self = &s;
    auto root = element_from_json(tree, [&](std::size_t index) -> std::optional<Handler> {
        if (index >= handlers.size()) return std::nullopt;
        const int id = handlers[index];
        return [self, id](const Json& event) { (void)self->call(id, event.dump(), true); };
    });
    Result<void> r = root ? s.session.mount(slot, *root) : Result<void>(std::unexpected(root.error()));
    if (!r) {
        for (int id : handlers) s.release(id);
        return fail(ctx, r.error().text());
    }
    // The view's old handlers go; the new ones stay while it is mounted.
    for (int id : s.slot_functions[slot]) s.release(id);
    s.slot_functions[slot] = std::move(handlers);
    return JS_UNDEFINED;
}

JSValue n_unmount(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto& s = impl(ctx);
    const std::string slot = text(ctx, argv[0]);
    auto r = s.session.unmount(slot);
    for (int id : s.slot_functions[slot]) s.release(id);
    s.slot_functions.erase(slot);
    if (!r) return fail(ctx, r.error().text());
    return JS_UNDEFINED;
}

JSValue n_document(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto& s = impl(ctx);
    const Json tree = Json::parse(text(ctx, argv[0]), nullptr, false);
    // A document only shown: its handlers are placeholders.
    auto root = element_from_json(tree, [](std::size_t) -> std::optional<Handler> { return [](const Json&) {}; });
    if (!root) return fail(ctx, root.error().text());
    return string(ctx, s.session.document(*root).dump());
}

abi::OmuiEffect effect_of(std::string_view e) {
    if (e == "read") return abi::OMUI_EFFECT_READ;
    if (e == "irreversible") return abi::OMUI_EFFECT_IRREVERSIBLE;
    if (e == "external") return abi::OMUI_EFFECT_EXTERNAL;
    return abi::OMUI_EFFECT_REVERSIBLE;
}

JSValue n_verb(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto& s = impl(ctx);
    const Json o = Json::parse(text(ctx, argv[1]), nullptr, false);
    Session::VerbSpec spec{text(ctx, argv[0]), o.value("summary", ""), effect_of(o.value("effect", "reversible")), o.value("answer", ""),
                           o.value("person", false)};
    const int id = s.keep(argv[2]);
    Script::Impl* self = &s;
    auto r = s.session.verb(std::move(spec), [self, id](const Json& args) -> Result<Json> {
        auto out = self->call(id, args.dump(), false);
        if (!out) return std::unexpected(Error{abi::OMUI_ERROR_REFUSED, "the verb threw"});
        Json v = Json::parse(*out, nullptr, false);
        return v.is_discarded() ? Json(nullptr) : v;
    });
    if (!r) {
        s.release(id);
        return fail(ctx, r.error().text());
    }
    return JS_UNDEFINED;
}

JSValue records(JSContext* ctx, const Result<std::vector<Json>>& r) {
    if (!r) return fail(ctx, r.error().text());
    return string(ctx, Json(*r).dump());
}

JSValue n_invoke(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    return records(ctx, impl(ctx).session.invoke(text(ctx, argv[0]), Json::parse(text(ctx, argv[1]), nullptr, false)));
}
JSValue n_act(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    return records(ctx, impl(ctx).session.act(Json::parse(text(ctx, argv[0]), nullptr, false)));
}
JSValue n_snapshot(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    return records(ctx, impl(ctx).session.snapshot(Json::parse(text(ctx, argv[0]), nullptr, false)));
}

JSValue n_find(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto n = impl(ctx).session.find(text(ctx, argv[0]));
    if (!n) return fail(ctx, n.error().text());
    return JS_NewInt64(ctx, static_cast<std::int64_t>(*n));
}

JSValue n_get(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto v = impl(ctx).session.get(static_cast<abi::OmuiNode>(integer(ctx, argv[0])), text(ctx, argv[1]));
    if (!v) return fail(ctx, v.error().text());
    return string(ctx, v->dump());
}

JSValue n_set(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    const Json props = Json::parse(text(ctx, argv[1]), nullptr, false);
    std::vector<std::pair<std::string, Json>> list;
    for (auto it = props.begin(); it != props.end(); ++it) list.emplace_back(it.key(), it.value());
    auto r = impl(ctx).session.set(static_cast<abi::OmuiNode>(integer(ctx, argv[0])), list);
    if (!r) return fail(ctx, r.error().text());
    return JS_UNDEFINED;
}

JSValue n_rows(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto r = impl(ctx).session.rows(text(ctx, argv[0]));
    if (!r) return fail(ctx, r.error().text());
    return string(ctx, r->dump());
}

JSValue n_splice(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto r = impl(ctx).session.splice(text(ctx, argv[0]), static_cast<std::size_t>(std::max<std::int64_t>(0, integer(ctx, argv[1]))),
                                      static_cast<std::size_t>(std::max<std::int64_t>(0, integer(ctx, argv[2]))),
                                      Json::parse(text(ctx, argv[3]), nullptr, false));
    if (!r) return fail(ctx, r.error().text());
    return JS_UNDEFINED;
}

// The runtime's theme (openmui.theme): a JSON list of layers.
JSValue n_theme(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    const Json layers = Json::parse(text(ctx, argv[0]), nullptr, false);
    std::vector<std::string> l;
    if (layers.is_array())
        for (const Json& x : layers)
            if (x.is_string()) l.push_back(x.get<std::string>());
    auto r = impl(ctx).session.theme(l);
    if (!r) return fail(ctx, r.error().text());
    return JS_UNDEFINED;
}

JSValue n_themes(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    const std::string out = impl(ctx).session.themes().dump();
    return JS_NewStringLen(ctx, out.data(), out.size());
}

// A native module's function (component-binding §8): its result as JSON text.
JSValue n_native_call(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto r = impl(ctx).session.native(text(ctx, argv[0]), text(ctx, argv[1]), Json::parse(text(ctx, argv[2]), nullptr, false));
    if (!r) return fail(ctx, r.error().text());
    const std::string out = r->dump();
    return JS_NewStringLen(ctx, out.data(), out.size());
}

// An `async` one: a promise of its result as JSON text. It runs on a worker
// thread; the promise settles on the script thread, and its jobs run then.
JSValue n_native_async(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    JSValue resolving[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) return promise;
    Script::Impl& s = impl(ctx);
    const int resolve = s.keep(resolving[0]), reject = s.keep(resolving[1]);
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    s.session.native_async(text(ctx, argv[0]), text(ctx, argv[1]), Json::parse(text(ctx, argv[2]), nullptr, false),
                           [impl = &s, alive = std::weak_ptr<bool>(s.alive), resolve, reject](lang::Result<Json> r) {
                               auto live = alive.lock();
                               if (!live || !*live) return;
                               const std::string out = r ? r->dump() : r.error().text();
                               JSValue arg = JS_NewStringLen(impl->ctx, out.data(), out.size());
                               JSValue f = impl->functions.at(r ? resolve : reject);
                               JSValue x = JS_Call(impl->ctx, f, JS_UNDEFINED, 1, &arg);
                               JS_FreeValue(impl->ctx, x);
                               JS_FreeValue(impl->ctx, arg);
                               impl->release(resolve);
                               impl->release(reject);
                               impl->run_jobs();
                           });
    return promise;
}

JSValue n_log(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    std::println(std::cerr, "js: {}", text(ctx, argv[0]));
    return JS_UNDEFINED;
}

std::string camel(std::string_view snake) {
    std::string out;
    bool up = false;
    for (char c : snake) {
        if (c == '_') {
            up = true;
            continue;
        }
        out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        up = false;
    }
    return out;
}

// openmui:native/<name>: a native module's functions, made from the
// catalogue (component-binding §8): `import { fuzzy } from "openmui:native/fuzzy"`.
std::optional<std::string> native_module_source(Script::Impl& s, std::string_view module) {
    const Json catalog = s.session.natives();
    if (!catalog.contains("modules") || !catalog["modules"].contains(std::string(module))) return std::nullopt;
    const Json& m = catalog["modules"][std::string(module)];
    std::string out = "import { __native } from \"openmui\";\n";
    std::string members;
    for (auto it = m["functions"].begin(); it != m["functions"].end(); ++it) {
        const std::string fn = camel(it.key());
        const bool async = it.value().value("async", false);
        out += std::format("export const {} = (...args) => __native({}, {}, {}, args);\n", fn, Json(std::string(module)).dump(), Json(it.key()).dump(),
                           async ? "true" : "false");
        members += (members.empty() ? "" : ", ") + fn;
        if (async) {
            // And waiting for it, where a promise cannot be awaited (a verb).
            out += std::format("export const {}Sync = (...args) => __native({}, {}, false, args);\n", fn, Json(std::string(module)).dump(),
                               Json(it.key()).dump());
            members += std::format(", {}Sync", fn);
        }
    }
    out += std::format("export const {0} = {{ {1} }};\nexport default {0};\n", module, members);
    return out;
}

JSModuleDef* load_module(JSContext* ctx, const char* name, void*) {
    auto& s = impl(ctx);
    std::string_view n = name;
    std::string source;
    if (n.starts_with("openmui:native/")) {
        auto m = native_module_source(s, n.substr(15));
        if (!m) {
            JS_ThrowReferenceError(ctx, "no native module `%s` (the program registers its native modules before scripts run)", name + 15);
            return nullptr;
        }
        source = std::move(*m);
        JSValue mod = JS_Eval(ctx, source.data(), source.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(mod)) return nullptr;
        auto* def = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(mod));
        JS_FreeValue(ctx, mod);
        return def;
    }
    if (n != "openmui") {
        JS_ThrowReferenceError(ctx, "no module `%s` (scripts import \"openmui\" and \"openmui:native/<name>\"; bundle the rest at build time)", name);
        return nullptr;
    }
    JSValue m = JS_Eval(ctx, s.module_source.data(), s.module_source.size(), "openmui", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(m)) return nullptr;
    auto* def = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(m));
    JS_FreeValue(ctx, m);
    return def;
}

}  // namespace

Script::Script(Session& session) : impl_(std::make_unique<Impl>(session)) {
    Impl* s = impl_.get();
    session.host().script([s] {
        s->rt = JS_NewRuntime();
        s->ctx = JS_NewContext(s->rt);
        JS_SetContextOpaque(s->ctx, s);
        JS_SetModuleLoaderFunc(s->rt, nullptr, load_module, s);
        JSValue global = JS_GetGlobalObject(s->ctx);
        JSValue n = JS_NewObject(s->ctx);
        const std::initializer_list<std::tuple<const char*, JSCFunction*, int>> natives{
            {"signalCreate", n_signal_create, 2}, {"signalGet", n_signal_get, 1}, {"signalSet", n_signal_set, 2},
            {"signalWatch", n_signal_watch, 2},   {"catalog", n_catalog, 0},       {"mount", n_mount, 3},
            {"unmount", n_unmount, 1},            {"document", n_document, 1},     {"verb", n_verb, 3},
            {"invoke", n_invoke, 2},              {"act", n_act, 1},               {"snapshot", n_snapshot, 1},
            {"find", n_find, 1},                  {"get", n_get, 2},               {"set", n_set, 2},
            {"rows", n_rows, 1},                  {"splice", n_splice, 4},         {"log", n_log, 1},
            {"nativeCall", n_native_call, 3},     {"nativeAsync", n_native_async, 3}, {"theme", n_theme, 1},
            {"themes", n_themes, 0}};
        for (const auto& [name, fn, arity] : natives) JS_SetPropertyStr(s->ctx, n, name, JS_NewCFunction(s->ctx, fn, name, arity));
        JS_SetPropertyStr(s->ctx, global, "__omui", n);
        JS_FreeValue(s->ctx, global);
        // The module: the prelude, and a builder for every registered component.
        std::string catalog;
        s->session.host().ui([&] {
            (void)abi::omuiCatalog(s->session.host().runtime(), [](void* u, const char* d, std::size_t k) { static_cast<std::string*>(u)->append(d, k); },
                                   &catalog);
        });
        s->module_source = std::string(prelude);
        const Json c = Json::parse(catalog, nullptr, false);
        if (c.is_object() && c.contains("components"))
            for (auto it = c["components"].begin(); it != c["components"].end(); ++it) {
                const std::string name = camel(it.key());
                // The prelude's own names win (its select is the component's builder too).
                if (s->module_source.find(std::format("export const {} ", name)) != std::string::npos ||
                    s->module_source.find(std::format("export function {}(", name)) != std::string::npos)
                    continue;
                s->module_source += std::format("export const {} = (props, children) => element(\"{}\", props, children);\n", name, it.key());
            }
    });
}

Script::~Script() {
    Impl* s = impl_.get();
    s->session.host().script([s] {
        *s->alive = false;  // an async native's answer arriving later finds no context
        for (auto& [id, f] : s->functions) JS_FreeValue(s->ctx, f);
        s->functions.clear();
        JS_FreeContext(s->ctx);
        JS_FreeRuntime(s->rt);
    });
}

void Script::on_error(std::function<void(std::string_view)> fn) { impl_->errors = std::move(fn); }

namespace {

Result<void> run_module(Script::Impl& s, const std::string& code, const std::string& name) {
    JSValue r = JS_Eval(s.ctx, code.data(), code.size(), name.c_str(), JS_EVAL_TYPE_MODULE);
    if (JS_IsException(r)) return std::unexpected(Error{abi::OMUI_ERROR_INVALID_ARGUMENT, s.exception_text()});
    s.run_jobs();
    // A module's evaluation is a promise: rejected is an error of the run.
    std::optional<std::string> rejected;
    if (JS_IsPromise(r) && JS_PromiseState(s.ctx, r) == JS_PROMISE_REJECTED) {
        JSValue reason = JS_PromiseResult(s.ctx, r);
        rejected = text(s.ctx, reason);
        JSValue stack = JS_GetPropertyStr(s.ctx, reason, "stack");
        if (!JS_IsUndefined(stack)) *rejected += "\n" + text(s.ctx, stack);
        JS_FreeValue(s.ctx, stack);
        JS_FreeValue(s.ctx, reason);
    }
    JS_FreeValue(s.ctx, r);
    if (rejected) return std::unexpected(Error{abi::OMUI_ERROR_INVALID_ARGUMENT, *rejected});
    return {};
}

}  // namespace

std::future<Result<void>> Script::run(std::string code, std::string name) {
    auto done = std::make_shared<std::promise<Result<void>>>();
    auto future = done->get_future();
    Impl* s = impl_.get();
    s->session.host().post([s, done, code = std::move(code), name = std::move(name)] { done->set_value(run_module(*s, code, name)); });
    return future;
}

std::future<Result<void>> Script::reload(std::string code, std::string name) {
    auto done = std::make_shared<std::promise<Result<void>>>();
    auto future = done->get_future();
    Impl* s = impl_.get();
    s->session.host().post([s, done, code = std::move(code), name = std::move(name)]() mutable {
        s->next_signal = 0;  // the module's signals are the last run's, in order
        // A module of the same name is cached: the new text gets a name of its own.
        static int generation = 0;
        done->set_value(run_module(*s, code, std::format("{}#{}", name, ++generation)));
    });
    return future;
}

}  // namespace omui::lang::js
