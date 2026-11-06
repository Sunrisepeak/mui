// openmui.lang.python — Python on the Script Binding Profile (component-binding
// §5.2, §7; D-46).
//
//     import openmui as omui
//     count = omui.signal(0)
//     def counter():
//         with omui.column(gap=8):
//             omui.text(text=omui.fmt("{} clicks", count))
//             omui.button(id="inc", text="+1", on_activated=lambda e: count.update(lambda v: v + 1))
//     omui.run(counter, title="Counter")
//
// `_openmui`, this extension, is the native half: signals, views as element
// trees, verbs, models and the semantic tree, over openmui.lang's Session. It
// is written against CPython's stable ABI (:capi). Every call it serves runs
// the Session's work on the script thread with the GIL released; a handler,
// a verb or a watch the runtime calls takes the GIL on the script thread. The
// `openmui` package (python/openmui) is the Python half: builders (also as
// context managers), expressions, `run`.
export module openmui.lang.python;

export import :capi;

import std;
import nlohmann.json;
import openmui.lang;

export namespace omui::lang::python {

// What the extension's host gives it: the window (or headless frames) the
// application runs in. `options` is {"title", "size": [w, h], "headless",
// "png", "frames"}; the answer is the exit code.
struct Hooks {
    std::function<int(const Json& options)> run;
    // One frame of a headless runtime (tests drive it): its time advances.
    std::function<void()> step;
    // Frames until the scripts have nothing left to do and nothing animates.
    std::function<void()> settle;
    // The pixels of the last frame, as a PNG file; an error message or empty.
    std::function<std::string(const std::string& file)> png;
    // The same pixels as a hash: FNV-1a over the RGBA rows, 16 hex digits --
    // what the examples' tests pin.
    std::function<std::string()> pixels;
};

// The `_openmui` module over a session. Nothing, with a Python exception set,
// when the interpreter lacks what it needs.
[[nodiscard]] capi::PyObject* create_module(Session& session, Hooks hooks);

}  // namespace omui::lang::python
