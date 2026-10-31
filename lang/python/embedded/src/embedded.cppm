// openmui.lang.python.embedded — Python embedded in a C++ program, with
// PocketPy, on the Script Binding Profile (component-binding §5.2, §7).
//
//     import openmui as omui
//
//     count = omui.signal(0)
//
//     def counter():
//         with omui.column(gap=8):
//             omui.text(text=omui.fmt("{} clicks", count))
//             omui.button(id="inc", text="+1", on_activated=lambda e: count.update(lambda v: v + 1))
//
//     omui.mount("main", counter)
//
// The module `openmui` is host mode's (lang/python) in PocketPy's subset of
// Python 3, so a script reads the same in both:
// - builders: omui.<component>(*children, props…, on_<event>=fn, animate={…}),
//   one for every registered component by its contract name (text_box), as
//   calls or as `with` blocks;
// - signals: omui.signal(v, key=None) with get, set, update, watch, value;
//   Python's operators and fmt / call / select / eq … / and_ / or_ / not_
//   make expressions the runtime evaluates;
// - regions: when, each(model, lambda row: …, where=…), slot;
// - mount / mount_document / unmount / document, verb (a decorator too) /
//   invoke, model, find / act / snapshot, tr, theme / themes, log, native.
// A refused call raises omui.OmuiError with the runtime's message.
export module openmui.lang.python.embedded;

import std;
import openmui.lang;
import openmui.player;

export namespace omui::lang::python::embedded {

class Script {
public:
    // A PocketPy VM on the session's script thread, with the `openmui`
    // module. Every Script of the process shares one interpreter (PocketPy
    // has one current VM and no threads): each has a VM of its own, sixteen
    // at most, and they take turns; a Script waiting on the runtime lets
    // the others run.
    explicit Script(Session& session);
    ~Script();
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;

    // Runs a script in the module __main__ on the script thread; the future
    // answers when it ran. An exception is an error with its traceback (the
    // file `name` and the line); SystemExit ends the script, an error when
    // its code is not 0.
    [[nodiscard]] std::future<Result<void>> run(std::string code, std::string name = "<script>");
    // Runs it again in place (hot reload): its signals keep their values
    // (the n-th omui.signal of the script is the n-th of the last run, or the
    // one of the same key), its views are reconciled, its handlers, watches
    // and verbs replaced; its globals stay unless it assigns them again.
    [[nodiscard]] std::future<Result<void>> reload(std::string code, std::string name = "<script>");

    // Where errors raised in handlers, watches and verbs go (default:
    // standard error).
    void on_error(std::function<void(std::string_view)> fn);
    // How much processor time a run, a handler or a verb may take before it
    // is stopped with a TimeoutError (PocketPy's watchdog; default 5 s, zero
    // for no limit). The process's time, not only the script's: a generous
    // bound for a runaway loop, not a measure.
    void budget(std::chrono::milliseconds limit);

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;  // shared with the runs queued for it (they find it gone, not freed)
};

// Python for the player: `lang = "py"`, *.py.
[[nodiscard]] player::Language language();

}  // namespace omui::lang::python::embedded
