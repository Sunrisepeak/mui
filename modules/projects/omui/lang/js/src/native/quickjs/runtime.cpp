// The `openmui` module's native half (`__omui`) and the QuickJS-ng runtime.
// Everything here runs on the script thread: QuickJS is single-threaded.
// The JavaScript functions the runtime may call later (handlers, verbs,
// watches) are kept in a table by id; what reaches the runtime is the id.
// QuickJS-ng's API is the module mcpplibs.mwrap.quickjs_ng (quickjs-ng-m):
// no header here.
module omui.lang.js;

import std;
import mcpplibs.mwrap.quickjs_ng;
import mcpplibs.os;
import nlohmann.json;
import omui.abi;
import omui.lang;
import omui.lang.ts;
import mlibs.ts;
import :prelude;
import :natives;

namespace omui::lang::js {

using namespace mcpplibs::mwrap::quickjs_ng;          // quickjs.h's static inline API (JS_IsException, JS_ToCString …)
using namespace mcpplibs::mwrap::quickjs_ng::macros;  // its macros (JS_UNDEFINED, JS_EVAL_TYPE_MODULE …)

struct Script::Impl final : Engine {
    Session&                                    session;
    JSRuntime*                                  rt = nullptr;
    JSContext*                                  ctx = nullptr;
    std::function<void(std::string_view)>       errors = [](std::string_view m) { std::println(std::cerr, "openmui js: {}", m); };
    std::string                                 module_source;  // the prelude and a builder per component
    std::map<int, JSValue>                      functions;
    int                                         next_function = 1;
    Natives                                     natives{session, *this};  // `__omui`'s table, its state (signals, watches, views' handlers)
    std::shared_ptr<bool>                       alive = natives.alive;  // false once the script is destroyed
    // Its modules by path (hot reload, Live Design): the text an edit gives a
    // file instead of the file's, and each file's version — a changed module
    // gets a name of its own, so the next run evaluates it again and the rest
    // stay as they are — and the files it has loaded. On the script thread.
    std::map<std::string, std::string>          overrides;
    std::map<std::string, int>                  versions;
    std::set<std::string>                       loaded;
    int                                         active = 0;  // its JavaScript on the script thread's stack
    std::map<std::string, std::string>          ran;  // each script's text as it last ran (keep_built_at: where its elements were built)

    explicit Impl(Session& s) : session(s) {
        // The text that ran for a file: a script's (its JavaScript keeps the TypeScript's places), an edited module's, the file's.
        natives.text_of = [this](std::string_view file) -> std::optional<std::string> {
            if (const auto it = ran.find(std::string(file)); it != ran.end()) return it->second;
            if (const auto it = overrides.find(std::string(file)); it != overrides.end()) return it->second;
            if (auto read = mcpplibs::os::fs::read(mcpplibs::os::fs::from_utf8(file))) return std::move(*read);
            return std::nullopt;
        };
    }

    // The context and the runtime let go: once, never while its JavaScript
    // is on the stack (Script::~Script waits for that, or the outermost
    // Inside lets them go when it returns).
    void close() {
        if (!rt) return;
        for (auto& [id, f] : functions) JS_FreeValue(ctx, f);
        functions.clear();
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        ctx = nullptr;
        rt = nullptr;
    }
    // Around the script's JavaScript: what a teardown waits for.
    struct Inside {
        Impl& s;
        explicit Inside(Impl& i) : s(i) { ++s.active; }
        ~Inside() {
            if (--s.active == 0 && !*s.alive) s.close();  // destroyed while it ran (from its own thread): it goes now
        }
        Inside(const Inside&) = delete;
        Inside& operator=(const Inside&) = delete;
    };

    int keep(JSValueConst f) {
        const int id = next_function++;
        functions.emplace(id, JS_DupValue(ctx, f));
        return id;
    }
    void release(int id) override {
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
    std::optional<std::string> call(int id, std::string_view arg, bool parse) override {
        Inside in(*this);
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

// ── __omui: the table's (natives.cpp), bound to QuickJS ──

// One template, not three overloads: choosing among overloads makes libc++ try converting a Result<void> into a
// Result<std::string>, which is a hard error there (std::string's constructor from a `const void&`).
template <class T>
JSValue answer(JSContext* ctx, const Result<T>& r) {
    if (!r) return fail(ctx, r.error().text());
    if constexpr (std::is_void_v<T>) return JS_UNDEFINED;
    else if constexpr (std::is_same_v<T, std::string>) return string(ctx, *r);
    else return JS_NewInt64(ctx, static_cast<std::int64_t>(*r));
}

JSValue n_signal_create(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return answer(ctx, impl(ctx).natives.signal_create(text(ctx, argv[0]), argc > 1 ? text(ctx, argv[1]) : std::string()));
}
JSValue n_signal_get(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return string(ctx, impl(ctx).natives.signal_get(integer(ctx, argv[0]))); }
JSValue n_signal_set(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    impl(ctx).natives.signal_set(integer(ctx, argv[0]), text(ctx, argv[1]));
    return JS_UNDEFINED;
}
JSValue n_signal_watch(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto& s = impl(ctx);
    if (!JS_IsFunction(ctx, argv[1])) return fail(ctx, "watch(fn)");
    s.natives.signal_watch(integer(ctx, argv[0]), s.keep(argv[1]));
    return JS_UNDEFINED;
}
JSValue n_catalog(JSContext* ctx, JSValueConst, int, JSValueConst*) { return string(ctx, impl(ctx).natives.catalog()); }
JSValue n_mount(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto& s = impl(ctx);
    std::vector<int> handlers;
    JSValue length = JS_GetPropertyStr(ctx, argv[2], "length");
    const std::int64_t count = integer(ctx, length);
    JS_FreeValue(ctx, length);
    for (std::int64_t i = 0; i < count; ++i) {
        JSValue f = JS_GetPropertyUint32(ctx, argv[2], static_cast<std::uint32_t>(i));
        handlers.push_back(s.keep(f));
        JS_FreeValue(ctx, f);
    }
    return answer(ctx, s.natives.mount(text(ctx, argv[0]), text(ctx, argv[1]), std::move(handlers)));
}
JSValue n_unmount(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.unmount(text(ctx, argv[0]))); }
JSValue n_document(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.document(text(ctx, argv[0]))); }
JSValue n_verb(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    auto& s = impl(ctx);
    return answer(ctx, s.natives.verb(text(ctx, argv[0]), text(ctx, argv[1]), s.keep(argv[2])));
}
JSValue n_task(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.task(text(ctx, argv[0]))); }
JSValue n_invoke(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.invoke(text(ctx, argv[0]), text(ctx, argv[1]))); }
JSValue n_act(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.act(text(ctx, argv[0]))); }
JSValue n_snapshot(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.snapshot(text(ctx, argv[0]))); }
JSValue n_find(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.find(text(ctx, argv[0]))); }
JSValue n_get(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.get(integer(ctx, argv[0]), text(ctx, argv[1]))); }
JSValue n_set(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.set(integer(ctx, argv[0]), text(ctx, argv[1]))); }
JSValue n_rows(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.rows(text(ctx, argv[0]))); }
JSValue n_splice(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    return answer(ctx, impl(ctx).natives.splice(text(ctx, argv[0]), integer(ctx, argv[1]), integer(ctx, argv[2]), text(ctx, argv[3])));
}
JSValue n_theme(JSContext* ctx, JSValueConst, int, JSValueConst* argv) { return answer(ctx, impl(ctx).natives.theme(text(ctx, argv[0]))); }
JSValue n_themes(JSContext* ctx, JSValueConst, int, JSValueConst*) { return string(ctx, impl(ctx).natives.themes()); }
// Whether the elements it builds carry where they were built (development mode).
JSValue n_keeps_built_at(JSContext* ctx, JSValueConst, int, JSValueConst*) { return JS_NewBool(ctx, impl(ctx).natives.keep_built_at); }
JSValue n_native_call(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    return answer(ctx, impl(ctx).natives.native_call(text(ctx, argv[0]), text(ctx, argv[1]), text(ctx, argv[2])));
}
// An `async` one: a promise of its result as JSON text. It runs on a worker thread; the promise settles on the script
// thread, and its jobs run then.
JSValue n_native_async(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    JSValue resolving[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) return promise;
    Script::Impl& s = impl(ctx);
    const int resolve = s.keep(resolving[0]), reject = s.keep(resolving[1]);
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    s.natives.native_async(text(ctx, argv[0]), text(ctx, argv[1]), text(ctx, argv[2]), [impl = &s, resolve, reject](Result<std::string> r) {
        Script::Impl::Inside in(*impl);
        const std::string out = r ? *r : r.error().text();
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


// A module's name: "openmui" and "openmui:…" as they are; one by path, its
// file — the name it is imported by, from the importing module's directory,
// as it is, or with .ts, .mts, .js, .mjs, or its directory's index — with
// "#v<n>" when the file has changed n times since the script began.
char* normalize_module(JSContext* ctx, const char* base, const char* name, void*) {
    auto& s = impl(ctx);
    const std::string_view n = name;
    if (!(n.starts_with("./") || n.starts_with("../") || n.starts_with("/"))) return js_strdup(ctx, name);
    std::string_view from = base;
    from = from.substr(0, from.find('#'));
    const auto wanted = (n.starts_with("/") ? mcpplibs::os::fs::from_utf8(n) : mcpplibs::os::fs::from_utf8(from).parent_path() / mcpplibs::os::fs::from_utf8(n))
                            .lexically_normal();
    std::string file = mcpplibs::os::fs::utf8(wanted);
    for (const std::string_view suffix : {"", ".ts", ".mts", ".js", ".mjs", "/index.ts", "/index.js"}) {
        const std::string candidate = mcpplibs::os::fs::utf8(wanted) + std::string(suffix);
        std::error_code error;
        if (s.overrides.contains(candidate) || std::filesystem::is_regular_file(mcpplibs::os::fs::from_utf8(candidate), error)) {
            file = candidate;
            break;
        }
    }
    if (const auto version = s.versions.find(file); version != s.versions.end() && version->second > 0) file += std::format("#v{}", version->second);
    return js_strdup(ctx, file.c_str());
}

JSModuleDef* load_module(JSContext* ctx, const char* name, void*) {
    auto& s = impl(ctx);
    std::string_view n = name;
    std::string source;
    if (n.starts_with("openmui:native/")) {
        auto m = native_module(s.session, n.substr(15));
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
        // The program's own module, by path (normalize_module resolved it to
        // its file, a changed one's name carrying its version): the text an
        // edit gave it, or the file's; TypeScript with its types blanked.
        const std::string file(n.substr(0, n.find('#')));
        std::expected<std::string, mlibs::ts::TsFailure> code;
        if (const auto edited = s.overrides.find(file); edited != s.overrides.end()) {
            if (file.ends_with(".ts") || file.ends_with(".mts")) code = mlibs::ts::compile_source(edited->second, file);
            else code = edited->second;
        } else {
            const auto path = mcpplibs::os::fs::from_utf8(file);
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) {
                JS_ThrowReferenceError(ctx, "no module `%s` (scripts import \"openmui\", \"openmui:native/<name>\" and their own files by path)", name);
                return nullptr;
            }
            const auto extension = path.extension();
            code = extension == ".ts" || extension == ".mts" ? mlibs::ts::compile(path) : [&]() -> std::expected<std::string, mlibs::ts::TsFailure> {
                std::ifstream in(path, std::ios::binary);
                return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            }();
        }
        if (!code) {
            JS_ThrowSyntaxError(ctx, "%s", code.error().message.c_str());
            return nullptr;
        }
        s.loaded.insert(file);
        JSValue module = JS_Eval(ctx, code->data(), code->size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(module)) return nullptr;
        auto* def = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(module));
        JS_FreeValue(ctx, module);
        return def;
    }
    JSValue m = JS_Eval(ctx, s.module_source.data(), s.module_source.size(), "openmui", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(m)) return nullptr;
    auto* def = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(m));
    JS_FreeValue(ctx, m);
    return def;
}

}  // namespace

Script::Script(Session& session) : impl_(std::make_shared<Impl>(session)) {
    Impl* s = impl_.get();
    session.host().script([s] {
        s->rt = JS_NewRuntime();
        s->ctx = JS_NewContext(s->rt);
        JS_SetContextOpaque(s->ctx, s);
        JS_SetModuleLoaderFunc(s->rt, normalize_module, load_module, s);
        JSValue global = JS_GetGlobalObject(s->ctx);
        JSValue n = JS_NewObject(s->ctx);
        const std::initializer_list<std::tuple<const char*, JSCFunction*, int>> natives{
            {"signalCreate", n_signal_create, 2}, {"signalGet", n_signal_get, 1}, {"signalSet", n_signal_set, 2},
            {"signalWatch", n_signal_watch, 2},   {"catalog", n_catalog, 0},       {"mount", n_mount, 3},
            {"unmount", n_unmount, 1},            {"document", n_document, 1},     {"verb", n_verb, 3},        {"task", n_task, 1},
            {"invoke", n_invoke, 2},              {"act", n_act, 1},               {"snapshot", n_snapshot, 1},
            {"find", n_find, 1},                  {"get", n_get, 2},               {"set", n_set, 2},
            {"rows", n_rows, 1},                  {"splice", n_splice, 4},         {"log", n_log, 1},
            {"nativeCall", n_native_call, 3},     {"nativeAsync", n_native_async, 3}, {"theme", n_theme, 1},
            {"themes", n_themes, 0},              {"keepsBuiltAt", n_keeps_built_at, 0}};
        for (const auto& [name, fn, arity] : natives) JS_SetPropertyStr(s->ctx, n, name, JS_NewCFunction(s->ctx, fn, name, arity));
        JS_SetPropertyStr(s->ctx, global, "__omui", n);
        JS_FreeValue(s->ctx, global);
        // The module: the prelude, and a builder for every registered component.
        s->module_source = openmui_module(s->session);
    });
}

// Safe while it loads: its JavaScript on the script thread's stack (a run
// waiting on this thread for the runtime) finishes before the context goes;
// the runs still queued, an async native's answer and what the runtime calls
// later find it gone.
Script::~Script() {
    Impl* s = impl_.get();
    s->session.host().retire([s] {
        *s->alive = false;
        if (s->active > 0) return false;
        s->close();
        return true;
    });
}

void Script::on_error(std::function<void(std::string_view)> fn) { impl_->errors = std::move(fn); }

bool let_page_run() {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    return true;
}

namespace {

Error gone() { return Error{abi::OMUI_ERROR_REFUSED, "the script is gone"}; }

Result<void> run_module(Script::Impl& s, const std::string& code, const std::string& name) {
    if (!*s.alive) return std::unexpected(gone());
    Script::Impl::Inside in(s);
    if (s.natives.keep_built_at) s.ran[name.substr(0, name.find('#'))] = code;
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
    // The script's state is shared with the run queued: one queued when the
    // script is destroyed finds it gone, not freed.
    std::shared_ptr<Impl> s = impl_;
    s->session.host().post([s, done, code = std::move(code), name = std::move(name)] { done->set_value(run_module(*s, code, name)); });
    return future;
}

namespace {

// A reload, on the script thread.
Result<void> reload_module(Script::Impl& s, const std::string& code, const std::string& name) {
    if (!*s.alive) return std::unexpected(gone());
    s.natives.reload();  // the run's signals found again in order, its watches let go
    // A module of the same name is cached: the new text gets a name of its own.
    static int generation = 0;
    return run_module(s, code, std::format("{}#{}", name, ++generation));
}

}  // namespace

std::future<Result<void>> Script::reload(std::string code, std::string name) {
    auto done = std::make_shared<std::promise<Result<void>>>();
    auto future = done->get_future();
    std::shared_ptr<Impl> s = impl_;
    s->session.host().post([s, done, code = std::move(code), name = std::move(name)] { done->set_value(reload_module(*s, code, name)); });
    return future;
}

void Script::module_changed(const std::filesystem::path& file, std::optional<std::string> text) {
    std::shared_ptr<Impl> s = impl_;
    const std::string name = mcpplibs::os::fs::utf8(file.lexically_normal());
    s->session.host().script([&] {
        if (text)
            s->overrides[name] = std::move(*text);
        else
            s->overrides.erase(name);
        ++s->versions[name];
    });
}

std::vector<std::filesystem::path> Script::modules() const {
    std::shared_ptr<Impl> s = impl_;
    return s->session.host().script([&] {
        std::vector<std::filesystem::path> out;
        for (const auto& file : s->loaded) out.push_back(mcpplibs::os::fs::from_utf8(file));
        return out;
    });
}

void Script::keep_built_at(bool on) {
    std::shared_ptr<Impl> s = impl_;
    s->session.host().script([&] { s->natives.keep_built_at = on; });
}

Result<void> Script::reload_now(std::string code, std::string name) {
    std::shared_ptr<Impl> s = impl_;
    return s->session.host().script([&]() -> Result<void> {
        // Asked while its last run waits on the runtime's thread (a mount, a verb), this runs nested in that
        // run: a module evaluated inside another's evaluation is what QuickJS cannot do. Refused; asked again
        // once the run is done, it runs.
        if (s->active > 0)
            return std::unexpected(Error{abi::OMUI_ERROR_REFUSED, "the script is still running (its last run waits on the application); try again when it is done"});
        return reload_module(*s, code, name);
    });
}

}  // namespace omui::lang::js
