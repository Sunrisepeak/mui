// The browser host's loop, off the Web (no page: stepped by hand): a frame
// when something wants one and none while nothing moves; the frame drawn by
// the CPU renderer; input — keys, the pointer, a gamepad — reaching the
// focus as a page's would.
import std;
import openmui.host.web;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

typeset::FontCollection& fonts() {
    static typeset::FontCollection f = [] {
        typeset::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"host.web"};

    s.test("off the Web: no page, the loop stepped by hand", [] {
        check(!in_browser());
        Web w({.size = {200, 100}, .fonts = &fonts()});
        auto color = signal(std::string("#ff0000"));
        w.mount("main", column({.padding = 20}, {box({.id = "swatch", .width = 40, .height = 40,
                                                      .background = Prop<Fill>(Source{std::nullopt, [color] { return Value(color.get()); }, {}})})}));
        check(w.step(), "the first frame");
        check_eq(w.frames_shown(), std::size_t{1});
        const auto& px = w.pixels();
        check(px.width() == 200 && px.height() == 100, std::format("{}x{}", px.width(), px.height()));
        check(px.at(40, 40).r == 255 && px.at(40, 40).g == 0 && px.at(5, 5).g == 255, "the CPU frame");
        (void)w.step();
        check(w.frames_shown() == 1, "nothing changed: nothing shown");
        color.set("#0000ff");
        (void)w.step();
        check(w.pixels().at(40, 40).b == 255 && w.frames_shown() == 2, "a change: a frame");
    });

    s.test("input as the page gives it: a key and a gamepad to the focus", [] {
        Web w({.size = {200, 100}, .fonts = &fonts()});
        std::vector<std::string> seen;
        w.mount("main", column({}, {painter({.id = "game", .width = 100, .height = 60, .focusable = true,
                                             .on_event = [&](EventContext&, const InputEvent& e) {
                                                 if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down)
                                                     seen.push_back(std::string(name(k->key)));
                                                 if (const auto* g = std::get_if<GamepadEvent>(&e); g && g->kind == GamepadKind::down)
                                                     seen.push_back(std::string(name(g->button)));
                                                 return true;
                                             }})}));
        (void)w.step();
        w.runtime().focus(w.runtime().find("main/game"));
        std::array<InputEvent, 3> events{KeyEvent{.kind = KeyKind::down, .key = Key::space},
                                         GamepadEvent{.kind = GamepadKind::connected, .device = 1},
                                         GamepadEvent{.kind = GamepadKind::down, .device = 1, .button = GamepadButton::south}};
        w.synthesize(events);
        check(w.step(), "input: a frame");
        check(seen == std::vector<std::string>{"Space", "south"}, std::format("{}", seen));
    });

    return s.run(argc, argv);
}
