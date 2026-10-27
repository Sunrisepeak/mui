// openmui.lang.lua — Lua 5.4 on the Script Binding Profile (component-binding
// §5.2, §7).
//
//     local count = omui.signal(0)
//     omui.mount("main", omui.Column { gap = 8,
//       omui.Text   { text = omui.fmt("{} clicks", count) },
//       omui.Button { id = "inc", text = "+1", on_activated = function() count:update(function(v) return v + 1 end) end },
//     })
//
// The module is `omui` (a global, and `require "openmui"`):
// - builders: omui.<Component> { props…, children…, on_<event> = fn, animate = {…} },
//   by any registered component's name (PascalCase or the contract's);
// - signals: omui.signal(v [, key]) with :get, :set, :update, :watch; arithmetic and
//   omui.fmt / select / eq / not_ / call make expressions the runtime evaluates;
// - regions: omui.when(cond, {then…}, {else…}), omui.each(model, function(row) … end [, where]),
//   omui.slot(name);
// - omui.mount / unmount, omui.verb / invoke, omui.model(name), omui.find / act / snapshot,
//   omui.tr, omui.null, omui.log.
// Errors are values: a call that fails answers nil and a message.
export module openmui.lang.lua;

import std;
import openmui.lang;
import openmui.player;

export namespace omui::lang::lua {

class Script {
public:
    // A Lua state on the session's script thread, with the `omui` module.
    explicit Script(Session& session);
    ~Script();
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;

    // Runs a chunk on the script thread; the future answers when it ran.
    [[nodiscard]] std::future<Result<void>> run(std::string code, std::string name = "=script");
    // Runs it again in place (hot reload): its signals keep their values
    // (the n-th omui.signal of the chunk is the n-th of the last run, or the
    // one of the same key), its views are reconciled, its handlers replaced.
    [[nodiscard]] std::future<Result<void>> reload(std::string code, std::string name = "=script");

    // Where errors raised in handlers and verbs go (default: standard error).
    void on_error(std::function<void(std::string_view)> fn);

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// Lua for the player: `lang = "lua"`, *.lua.
[[nodiscard]] player::Language language();

}  // namespace omui::lang::lua
