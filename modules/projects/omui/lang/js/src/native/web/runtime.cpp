// JavaScript on the Web (D-237, G1): the page's own engine runs a script — no second engine in the program. This is
// the program's half of the binding; src/js/omui_script.js (an Emscripten library, build.mcpp) is the page's.
//
// The page has one thread, and the runtime's: a script's natives (`__omui`) reach the session at once (the host runs
// script work on the runtime's thread where there are no threads, G0). A module's evaluation is the page's import(),
// asynchronous, so a run's future becomes ready when the page settles it — the player collects runs at its frame
// boundary and never waits on one. The script's functions are the page's to keep; the session names them by number
// (Engine), as with QuickJS. Text in, text out: the natives speak JSON (natives.cppm).
module omui.lang.js;

import std;
import mcpplibs.os;
import nlohmann.json;
import omui.abi;
import omui.lang;
import mlibs.ts;
import :natives;

extern "C" {
void  omui_script_begin(int script, const char* module, char* (*dispatch)(int, const char*, const char*),
                        void (*settled)(int, int, int, char*));
void  omui_script_run(int script, int run, const char* name, const char* code);
char* omui_script_call(int script, int function, const char* argument, int parse);
void  omui_script_release(int script, int function);
void  omui_script_end(int script);
int   omui_script_pause();
int   omui_script_can_pause();
}

namespace omui::lang::js {

struct Script::Impl final : Engine {
    Session&                                       session;
    int                                            id;
    std::function<void(std::string_view)>          errors = [](std::string_view m) { std::println(std::cerr, "openmui js: {}", m); };
    Natives                                        natives{session, *this};
    std::shared_ptr<bool>                          alive = natives.alive;
    std::map<int, std::promise<Result<void>>>      runs;  // evaluating on the page, by number
    int                                            next_run = 1;
    std::map<std::string, std::string>             overrides;  // a module's text an edit gave it
    std::map<std::string, int>                     changes;    // how often a module was told it changed
    std::set<std::string>                          loaded;
    std::map<std::string, std::string>             ran;  // each script's text as it last ran (keep_built_at)

    Impl(Session& s, int number) : session(s), id(number) {
        natives.text_of = [this](std::string_view file) -> std::optional<std::string> {
            if (const auto it = ran.find(std::string(file)); it != ran.end()) return it->second;
            if (const auto it = overrides.find(std::string(file)); it != overrides.end()) return it->second;
            if (auto read = mcpplibs::os::fs::read(mcpplibs::os::fs::from_utf8(file))) return std::move(*read);
            return std::nullopt;
        };
    }

    std::optional<std::string> call(int function, std::string_view argument, bool parse) override {
        char* out = omui_script_call(id, function, std::string(argument).c_str(), parse ? 1 : 0);
        if (!out) return std::nullopt;
        std::string text(out);
        std::free(out);
        return text;
    }
    void release(int function) override { omui_script_release(id, function); }
};

namespace {

std::map<int, Script::Impl*>& scripts() {
    static std::map<int, Script::Impl*> all;
    return all;
}
int next_script = 1;
int dispatching = 0;  // natives answering a script now: the page's JavaScript is on the stack, which no pause unwinds

char* copied(const std::string& text) {
    auto* out = static_cast<char*>(std::malloc(text.size() + 1));
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
    return out;
}

std::string value(const Json& v) { return Json{{"v", v}}.dump(); }
std::string refused(std::string_view why) { return Json{{"e", why}}.dump(); }
// One function for every result (overloads on Result<void>, <string> and <int64_t> make libc++ try converting one
// into another, a hard error: red 134).
template <class T>
std::string answer(const Result<T>& r) {
    if (!r) return refused(r.error().text());
    if constexpr (std::is_void_v<T>) return value(nullptr);
    else return value(*r);
}

// A module a script imports: `openmui:native/<name>`, or one of its own files by path — from the importing module's
// directory, as it is or with .ts, .mts, .js, .mjs or its directory's index; TypeScript with its types blanked.
std::string module_of(Script::Impl& s, std::string_view specifier, std::string_view from) {
    if (specifier.starts_with("openmui:native/")) {
        auto text = native_module(s.session, specifier.substr(15));
        if (!text) return refused(std::format("no native module `{}` (the program registers its native modules before scripts run)", specifier.substr(15)));
        return value(Json{{"name", specifier}, {"code", *text}});
    }
    const auto wanted = (specifier.starts_with("/") ? mcpplibs::os::fs::from_utf8(specifier)
                                                    : mcpplibs::os::fs::from_utf8(from).parent_path() / mcpplibs::os::fs::from_utf8(specifier))
                            .lexically_normal();
    for (const std::string_view suffix : {"", ".ts", ".mts", ".js", ".mjs", "/index.ts", "/index.js"}) {
        const std::string file = mcpplibs::os::fs::utf8(wanted) + std::string(suffix);
        std::error_code error;
        const bool edited = s.overrides.contains(file);
        if (!edited && !std::filesystem::is_regular_file(mcpplibs::os::fs::from_utf8(file), error)) continue;
        std::string text = edited ? s.overrides.at(file) : mcpplibs::os::fs::read(mcpplibs::os::fs::from_utf8(file)).value_or("");
        if (file.ends_with(".ts") || file.ends_with(".mts")) {
            auto blanked = mlibs::ts::compile_source(text, file);
            if (!blanked) return refused(blanked.error().message);
            text = std::move(*blanked);
        }
        // The page knows a module by its text: one changed back to text it had before would be the module already
        // evaluated. Each change is a module of its own, evaluated again (as QuickJS does).
        if (const auto changed = s.changes.find(file); changed != s.changes.end()) text += std::format("\n// change {}", changed->second);
        s.loaded.insert(file);
        return value(Json{{"name", file}, {"code", text}});
    }
    return refused(std::format("no module `{}` (scripts import \"openmui\", \"openmui:native/<name>\" and their own files by path)", specifier));
}

// The `__omui` table's natives, from the page: their arguments as a JSON list, an envelope back.
char* dispatch(int script, const char* op_text, const char* arguments_text) {
    const auto found = scripts().find(script);
    if (found == scripts().end()) return copied(refused("the script is gone"));
    Script::Impl& s = *found->second;
    ++dispatching;
    const struct Done {
        ~Done() { --dispatching; }
    } done;
    const std::string_view op = op_text;
    const Json a = Json::parse(arguments_text, nullptr, false);
    const auto text = [&](std::size_t i) -> std::string {
        if (!a.is_array() || i >= a.size() || a[i].is_null()) return {};
        return a[i].is_string() ? a[i].get<std::string>() : a[i].dump();
    };
    const auto number = [&](std::size_t i) -> std::int64_t {
        return a.is_array() && i < a.size() && a[i].is_number() ? a[i].get<std::int64_t>() : 0;
    };
    std::string out;
    if (op == "signalCreate") out = answer(s.natives.signal_create(text(0), text(1)));
    else if (op == "signalGet") out = value(s.natives.signal_get(number(0)));
    else if (op == "signalSet") {
        s.natives.signal_set(number(0), text(1));
        out = value(nullptr);
    } else if (op == "signalWatch") {
        s.natives.signal_watch(number(0), static_cast<int>(number(1)));
        out = value(nullptr);
    }
    else if (op == "catalog") out = value(s.natives.catalog());
    else if (op == "mount") {
        std::vector<int> handlers;
        if (a.is_array() && a.size() > 2 && a[2].is_array())
            for (const auto& handler : a[2]) handlers.push_back(handler.get<int>());
        out = answer(s.natives.mount(text(0), text(1), std::move(handlers)));
    } else if (op == "unmount") out = answer(s.natives.unmount(text(0)));
    else if (op == "document") out = answer(s.natives.document(text(0)));
    else if (op == "verb") out = answer(s.natives.verb(text(0), text(1), static_cast<int>(number(2))));
    else if (op == "task") out = answer(s.natives.task(text(0)));
    else if (op == "invoke") out = answer(s.natives.invoke(text(0), text(1)));
    else if (op == "act") out = answer(s.natives.act(text(0)));
    else if (op == "snapshot") out = answer(s.natives.snapshot(text(0)));
    else if (op == "find") out = answer(s.natives.find(text(0)));
    else if (op == "get") out = answer(s.natives.get(number(0), text(1)));
    else if (op == "set") out = answer(s.natives.set(number(0), text(1)));
    else if (op == "rows") out = answer(s.natives.rows(text(0)));
    else if (op == "splice") out = answer(s.natives.splice(text(0), number(1), number(2), text(3)));
    else if (op == "theme") out = answer(s.natives.theme(text(0)));
    else if (op == "themes") out = value(s.natives.themes());
    else if (op == "copy") out = answer(s.natives.copy(text(0)));
    else if (op == "linkAgent") out = answer(s.natives.link_agent());
    else if (op == "agentLink") out = answer(s.natives.agent_link());
    else if (op == "nativeCall") out = answer(s.natives.native_call(text(0), text(1), text(2)));
    else if (op == "nativeAsync") {
        const int resolve = static_cast<int>(number(3)), reject = static_cast<int>(number(4));
        s.natives.native_async(text(0), text(1), text(2), [impl = &s, alive = s.alive, resolve, reject](Result<std::string> r) {
            if (!*alive) return;
            (void)impl->call(r ? resolve : reject, r ? *r : r.error().text(), false);
            impl->release(resolve);
            impl->release(reject);
        });
        out = value(nullptr);
    } else if (op == "keepsBuiltAt") out = value(s.natives.keep_built_at);
    else if (op == "log") {
        std::println(std::cerr, "js: {}", text(0));
        out = value(nullptr);
    }
    else if (op == "module") out = module_of(s, text(0), text(1));
    else out = refused(std::format("no native `{}`", op));
    return copied(out);
}

// A run the page evaluated: its result (an exception's text and stack when it threw).
void settled(int script, int run, int ok, char* text) {
    std::string said = text ? text : "";
    std::free(text);
    const auto found = scripts().find(script);
    if (found == scripts().end()) return;
    Script::Impl& s = *found->second;
    const auto pending = s.runs.find(run);
    if (pending == s.runs.end()) return;
    if (ok) {
        pending->second.set_value({});
    } else {
        s.errors(said);
        pending->second.set_value(std::unexpected(Error{abi::OMUI_ERROR_INVALID_ARGUMENT, said}));
    }
    s.runs.erase(pending);
}

std::future<Result<void>> evaluate(Script::Impl& s, const std::string& code, const std::string& name) {
    const int run = s.next_run++;
    auto future = s.runs[run].get_future();
    if (s.natives.keep_built_at) s.ran[name.substr(0, name.find('#'))] = code;
    omui_script_run(s.id, run, name.c_str(), code.c_str());
    return future;
}

Error gone() { return Error{abi::OMUI_ERROR_REFUSED, "the script is gone"}; }

}  // namespace

Script::Script(Session& session) : impl_(std::make_shared<Impl>(session, next_script++)) {
    Impl* s = impl_.get();
    scripts()[s->id] = s;
    // The module, the prelude and a builder for every registered component: the page's `openmui` for this script.
    const std::string module = openmui_module(session);
    omui_script_begin(s->id, module.c_str(), &dispatch, &settled);
}

Script::~Script() {
    Impl& s = *impl_;
    *s.alive = false;
    for (auto& [run, promise] : s.runs) promise.set_value(std::unexpected(gone()));
    s.runs.clear();
    scripts().erase(s.id);
    omui_script_end(s.id);
}

void Script::on_error(std::function<void(std::string_view)> fn) { impl_->errors = std::move(fn); }

bool let_page_run() { return omui_script_pause() == 1; }

std::future<Result<void>> Script::run(std::string code, std::string name) { return evaluate(*impl_, code, name); }

std::future<Result<void>> Script::reload(std::string code, std::string name) {
    impl_->natives.reload();  // the run's signals found again in order, its watches let go
    static int generation = 0;
    return evaluate(*impl_, code, std::format("{}#{}", name, ++generation));
}

// On the page a module's evaluation settles later: a reload waited for lets the page run until it has (Live Design's
// edits, answered with what changed) — in a program linked to suspend, and not from inside a script's native, where the
// page's JavaScript is on the stack (as QuickJS refuses a module evaluated inside another's run).
Result<void> Script::reload_now(std::string code, std::string name) {
    if (omui_script_can_pause() != 1)
        return std::unexpected(Error{abi::OMUI_ERROR_REFUSED,
                                     "on the page a module is evaluated asynchronously and this program cannot wait for it (link it with -sASYNCIFY); "
                                     "reload() returns its future"});
    if (dispatching > 0)
        return std::unexpected(Error{abi::OMUI_ERROR_REFUSED, "the script is still running (a native it called is answering); try again when it is done"});
    std::future<Result<void>> done = reload(std::move(code), std::move(name));
    while (done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) (void)let_page_run();
    return done.get();
}

void Script::module_changed(const std::filesystem::path& file, std::optional<std::string> text) {
    const std::string name = mcpplibs::os::fs::utf8(file.lexically_normal());
    if (text) impl_->overrides[name] = std::move(*text);
    else impl_->overrides.erase(name);
    ++impl_->changes[name];
}

std::vector<std::filesystem::path> Script::modules() const {
    std::vector<std::filesystem::path> out;
    for (const auto& file : impl_->loaded) out.push_back(mcpplibs::os::fs::from_utf8(file));
    return out;
}

void Script::keep_built_at(bool on) { impl_->natives.keep_built_at = on; }

}  // namespace omui::lang::js
