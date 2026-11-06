// openmui's events for a game: a key by its code (held, released); the
// pointer within the game's area, across and down from 0 to 1; a click as
// the mouse's button; focus lost lets everything go.
import std;
import omui.core;
import omui.view;
import xengine.input;
import xengine.ui;
import mlibs.testing;

using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite s{"xengine.ui"};

    s.test("keys by code, the pointer within the area, a click, focus lost", [] {
        xengine::Bindings b;
        b.key(xengine::ui::code(omui::Key::space), xengine::Button{0});
        b.key(xengine::ui::code(omui::Key::left), xengine::Axis{0, -1});
        b.mouse(1, xengine::Button{1});
        xengine::Sampler in(b);
        const omui::Rect area{100, 50, 400, 300};
        check(xengine::ui::feed(in, omui::KeyEvent{.kind = omui::KeyKind::down, .key = omui::Key::space}, area));
        check(xengine::ui::feed(in, omui::KeyEvent{.kind = omui::KeyKind::down, .key = omui::Key::left}, area));
        check(xengine::ui::feed(in, omui::PointerEvent{.kind = omui::PointerKind::move, .position = {200, 125}}, area));
        auto a = in.sample();
        check(a.down(0) && a.axes[0] == -32767, "the key's button and axis");
        check(a.pointer_x == 8192 && a.pointer_y == 8192, std::format("a quarter across and down: {} {}", a.pointer_x, a.pointer_y));
        xengine::ui::feed(in, omui::PointerEvent{.kind = omui::PointerKind::down, .position = {200, 125}, .button = omui::MouseButton::left}, area);
        check(in.sample().went_down(1), "a click");
        xengine::ui::feed(in, omui::PointerEvent{.kind = omui::PointerKind::move, .position = {20, 125}}, area);
        check(in.sample().pointer_x == -1, "outside the area: no pointer");
        xengine::ui::feed(in, omui::FocusEvent{.gained = false}, area);
        const auto gone = in.sample();
        check(gone.held == 0 && gone.axes[0] == 0, "focus lost: nothing held");
        check(!xengine::ui::feed(in, omui::TextEvent{"x"}, area), "text is the interface's");
    });

    s.test("a gamepad: its buttons and axes by their place in the standard set", [] {
        using omui::GamepadAxis, omui::GamepadButton, omui::GamepadEvent, omui::GamepadKind;
        xengine::Bindings b;
        b.pad_button(xengine::ui::pad(GamepadButton::south), xengine::Button{0});
        b.pad_button(xengine::ui::pad(GamepadButton::dpad_left), xengine::Axis{0, -1});
        b.pad_axis(xengine::ui::pad(GamepadAxis::left_x), xengine::Axis{1, 1});
        xengine::Sampler in(b);
        const omui::Rect area{0, 0, 100, 100};
        check(xengine::ui::feed(in, GamepadEvent{.kind = GamepadKind::connected, .device = 1}, area));
        xengine::ui::feed(in, GamepadEvent{.kind = GamepadKind::down, .device = 1, .button = GamepadButton::south}, area);
        xengine::ui::feed(in, GamepadEvent{.kind = GamepadKind::down, .device = 1, .button = GamepadButton::dpad_left}, area);
        xengine::ui::feed(in, GamepadEvent{.kind = GamepadKind::axis, .device = 1, .axis = GamepadAxis::left_x, .value = 0.5f}, area);
        const auto a = in.sample();
        check(a.went_down(0) && a.axes[0] == -32767 && a.axes[1] == 16384, std::format("{} {}", a.axes[0], a.axes[1]));
        xengine::ui::feed(in, GamepadEvent{.kind = GamepadKind::up, .device = 1, .button = GamepadButton::dpad_left}, area);
        check_eq(in.sample().axes[0], std::int16_t{0});
    });

    return s.run(argc, argv);
}
