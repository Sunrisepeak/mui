// `__omui`, the `openmui` module's native half (prelude.cppm), once for every engine a script runs on: QuickJS here,
// the page's own engine on the Web (D-237). Text in, text out (JSON, as the prelude speaks it); the script's functions
// (handlers, watches, verbs) are the engine's to keep, and this table names them by number. Everything here runs on
// the script's thread.
module omui.lang.js:natives;

import std;
import nlohmann.json;
import omui.abi;
import omui.lang;

namespace omui::lang::js {

// The engine a script's JavaScript runs on: it keeps the script's functions and calls them by number.
struct Engine {
    virtual ~Engine() = default;
    // A kept function called with one argument (a JSON text, parsed or as a string): its result as text, or nothing when
    // it threw (the engine reports it).
    virtual std::optional<std::string> call(int function, std::string_view argument, bool parse) = 0;
    virtual void release(int function) = 0;
};

class Natives {
public:
    Natives(Session& session, Engine& engine) : session_(session), engine_(engine) {}

    // signal(initial) and signal(initial, key): a run's signals in order (a reload finds the same ones, their values kept
    // unless the edit made them another kind of thing), a keyed one by its key.
    [[nodiscard]] Result<std::int64_t> signal_create(std::string_view initial, std::string_view key);
    [[nodiscard]] std::string signal_get(std::int64_t signal);
    void signal_set(std::int64_t signal, std::string_view value);
    void signal_watch(std::int64_t signal, int function);
    [[nodiscard]] std::string catalog();
    // A view mounted with its handlers (the engine's functions, in the order the tree's handler indices name them);
    // the slot's old handlers let go.
    [[nodiscard]] Result<void> mount(std::string_view slot, std::string_view tree, std::vector<int> handlers);
    [[nodiscard]] Result<void> unmount(std::string_view slot);
    [[nodiscard]] Result<std::string> document(std::string_view tree);
    [[nodiscard]] Result<void> verb(std::string_view name, std::string_view options, int function);
    [[nodiscard]] Result<std::string> invoke(std::string_view name, std::string_view arguments);
    [[nodiscard]] Result<std::string> act(std::string_view request);
    [[nodiscard]] Result<std::string> snapshot(std::string_view request);
    [[nodiscard]] Result<std::int64_t> find(std::string_view path);
    [[nodiscard]] Result<std::string> get(std::int64_t node, std::string_view property);
    [[nodiscard]] Result<void> set(std::int64_t node, std::string_view properties);
    [[nodiscard]] Result<std::string> rows(std::string_view model);
    [[nodiscard]] Result<void> splice(std::string_view model, std::int64_t at, std::int64_t removed, std::string_view rows);
    [[nodiscard]] Result<void> theme(std::string_view layers);
    [[nodiscard]] std::string themes();
    [[nodiscard]] Result<std::string> native_call(std::string_view module, std::string_view function, std::string_view arguments);
    // An async native: `settled` gets its result (or its refusal's text) on the script's thread.
    void native_async(std::string_view module, std::string_view function, std::string_view arguments, std::function<void(Result<std::string>)> settled);

    // A reload begins: the run's signals are found again in order, and its watches let go (the run makes its own).
    void reload();
    // Development mode: elements carry where they were built, moved to the call as written; `text_of` gives the text
    // that ran for a file (a script's, an edited module's), nothing to read the file.
    bool keep_built_at = false;
    std::function<std::optional<std::string>(std::string_view file)> text_of;
    // false once the script is destroyed: what a late watch, handler or verb finds.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

private:
    Session&                                   session_;
    Engine&                                    engine_;
    std::vector<abi::OmuiSignal>               ordered_;  // signal()'s results in order, kept across reloads
    std::size_t                                next_signal_ = 0;
    std::map<std::string, abi::OmuiSignal>     keyed_;
    std::map<std::string, std::vector<int>>    slot_functions_;  // a mounted view's handlers
    std::vector<std::pair<std::uint64_t, int>> watches_;         // this run's watches (token, function)
};

// The `openmui` module's text, whichever engine runs it: the prelude and a builder for every component the runtime
// has registered (its catalog; the prelude's own names win, its select being that component's builder too).
[[nodiscard]] std::string openmui_module(Session& session);

// `openmui:native/<name>`: a native module's functions, made from the catalogue (component-binding §8): `import {
// fuzzy } from "openmui:native/fuzzy"`; nothing when the program registered no such module.
[[nodiscard]] std::optional<std::string> native_module(Session& session, std::string_view module);

}  // namespace omui::lang::js
