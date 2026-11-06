// The browser host end to end, in a page: tests/js/wasm.test.mjs runs this
// program under node in a fake DOM and plays the person and the page's
// agent — an input method composing in the hidden textarea, keys, a paste,
// a screen reader's click in the mirror, window.omui's requests — reading
// what the program did back from the mirror, the live region and
// localStorage. Anywhere else (a native target, node without the fake
// page) there is no page, and nothing to do.
import std;
import openmui.host.web;

using namespace omui;

namespace {

Source bound(std::function<Value()> fn) { return Source{std::nullopt, std::move(fn), {}}; }
Source fixed(Value v) { return Source{std::move(v), {}, {}}; }

}  // namespace

int main() {
    if (!in_browser()) {
        std::println("page_test: no page here; tests/js/wasm.test.mjs runs it in one");
        return 0;
    }
    // The page's: they live as long as it does (run() does not return).
    static Signal<std::string> typed{std::string()};
    static Signal<std::string> composing{std::string()};
    static Signal<int>         presses{0};
    static Web w({.title = "openmui page test", .app = "org.openmui.page-test"});
    (void)w.store().set("visits", "1");
    w.mount("main", column({.padding = 10, .gap = 6}, {
        painter({.id = "note", .width = 160, .height = 24, .role = Role::text_box, .focusable = true,
                 .on_event = [](EventContext&, const InputEvent& e) {
                     if (const auto* t = std::get_if<TextEvent>(&e)) {
                         typed.set(typed.peek() + t->text);
                         return true;
                     }
                     if (const auto* p = std::get_if<PreeditEvent>(&e)) {
                         composing.set(p->text);
                         return true;
                     }
                     return false;
                 }})
            .prop("label", bound([] { return Value(std::format("Note: {}[{}]", typed.get(), composing.get())); })),
        box({.id = "press", .width = 60, .height = 24, .label = "Press", .on = {{"press", [](const Value&) { presses.set(presses.peek() + 1); }}}}),
        box({.id = "allow", .width = 60, .height = 24, .label = "Allow", .on = {{"press", [](const Value&) { presses.set(100); }}}})
            .prop("person", fixed(Value(true))),
        box({.id = "status", .width = 100, .height = 16})
            .prop("role", fixed(Value("status")))
            .prop("label", bound([] { return Value(std::format("Pressed {}", presses.get())); })),
    }));
    (void)w.step();
    const NodeId note = w.runtime().find("main/note");
    w.runtime().focus(note);
    w.runtime().text_input(note, Rect{4, 4, 1, 16});
    w.wake();  // changed outside a frame: the page's next turn shows it
    return w.run();
}
