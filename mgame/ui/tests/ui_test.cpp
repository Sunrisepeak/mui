// openmui's events for a game: a key by its code (held, released); the
// pointer within the game's area, across and down from 0 to 1; a click as
// the mouse's button; focus lost lets everything go.
import std;
import openmui.core;
import openmui.view;
import mgame.input;
import mgame.ui;
import openmui.testing;

using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"mgame.ui"};

    s.test("keys by code, the pointer within the area, a click, focus lost", [] {
        mgame::Bindings b;
        b.key(mgame::ui::code(omui::Key::space), mgame::Button{0});
        b.key(mgame::ui::code(omui::Key::left), mgame::Axis{0, -1});
        b.mouse(1, mgame::Button{1});
        mgame::Sampler in(b);
        const omui::Rect area{100, 50, 400, 300};
        check(mgame::ui::feed(in, omui::KeyEvent{.kind = omui::KeyKind::down, .key = omui::Key::space}, area));
        check(mgame::ui::feed(in, omui::KeyEvent{.kind = omui::KeyKind::down, .key = omui::Key::left}, area));
        check(mgame::ui::feed(in, omui::PointerEvent{.kind = omui::PointerKind::move, .position = {200, 125}}, area));
        auto a = in.sample();
        check(a.down(0) && a.axes[0] == -32767, "the key's button and axis");
        check(a.pointer_x == 8192 && a.pointer_y == 8192, std::format("a quarter across and down: {} {}", a.pointer_x, a.pointer_y));
        mgame::ui::feed(in, omui::PointerEvent{.kind = omui::PointerKind::down, .position = {200, 125}, .button = omui::MouseButton::left}, area);
        check(in.sample().went_down(1), "a click");
        mgame::ui::feed(in, omui::PointerEvent{.kind = omui::PointerKind::move, .position = {20, 125}}, area);
        check(in.sample().pointer_x == -1, "outside the area: no pointer");
        mgame::ui::feed(in, omui::FocusEvent{.gained = false}, area);
        const auto gone = in.sample();
        check(gone.held == 0 && gone.axes[0] == 0, "focus lost: nothing held");
        check(!mgame::ui::feed(in, omui::TextEvent{"x"}, area), "text is the interface's");
    });

    return s.run(argc, argv);
}
