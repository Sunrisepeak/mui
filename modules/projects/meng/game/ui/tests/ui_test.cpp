// openmui's events for a game: a key by its code (held, released); the
// pointer within the game's area, across and down from 0 to 1; a click as
// the mouse's button; focus lost lets everything go.
import std;
import omui.core;
import omui.view;
import meng.input;
import meng.ui;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"meng.ui", [] {

    "keys by code, the pointer within the area, a click, focus lost"_test = [] {
        meng::Bindings b;
        b.key(meng::ui::code(omui::Key::space), meng::Button{0});
        b.key(meng::ui::code(omui::Key::left), meng::Axis{0, -1});
        b.mouse(1, meng::Button{1});
        meng::Sampler in(b);
        const omui::Rect area{100, 50, 400, 300};
        expect(meng::ui::feed(in, omui::KeyEvent{.kind = omui::KeyKind::down, .key = omui::Key::space}, area));
        expect(meng::ui::feed(in, omui::KeyEvent{.kind = omui::KeyKind::down, .key = omui::Key::left}, area));
        expect(meng::ui::feed(in, omui::PointerEvent{.kind = omui::PointerKind::move, .position = {200, 125}}, area));
        auto a = in.sample();
        expect(a.down(0) && a.axes[0] == -32767) << "the key's button and axis";
        expect(a.pointer_x == 8192 && a.pointer_y == 8192) << std::format("a quarter across and down: {} {}", a.pointer_x, a.pointer_y);
        meng::ui::feed(in, omui::PointerEvent{.kind = omui::PointerKind::down, .position = {200, 125}, .button = omui::MouseButton::left}, area);
        expect(in.sample().went_down(1)) << "a click";
        meng::ui::feed(in, omui::PointerEvent{.kind = omui::PointerKind::move, .position = {20, 125}}, area);
        expect(in.sample().pointer_x == -1) << "outside the area: no pointer";
        meng::ui::feed(in, omui::FocusEvent{.gained = false}, area);
        const auto gone = in.sample();
        expect(gone.held == 0 && gone.axes[0] == 0) << "focus lost: nothing held";
        expect(!meng::ui::feed(in, omui::TextEvent{"x"}, area)) << "text is the interface's";
    };

    "a gamepad: its buttons and axes by their place in the standard set"_test = [] {
        using omui::GamepadAxis, omui::GamepadButton, omui::GamepadEvent, omui::GamepadKind;
        meng::Bindings b;
        b.pad_button(meng::ui::pad(GamepadButton::south), meng::Button{0});
        b.pad_button(meng::ui::pad(GamepadButton::dpad_left), meng::Axis{0, -1});
        b.pad_axis(meng::ui::pad(GamepadAxis::left_x), meng::Axis{1, 1});
        meng::Sampler in(b);
        const omui::Rect area{0, 0, 100, 100};
        expect(meng::ui::feed(in, GamepadEvent{.kind = GamepadKind::connected, .device = 1}, area));
        meng::ui::feed(in, GamepadEvent{.kind = GamepadKind::down, .device = 1, .button = GamepadButton::south}, area);
        meng::ui::feed(in, GamepadEvent{.kind = GamepadKind::down, .device = 1, .button = GamepadButton::dpad_left}, area);
        meng::ui::feed(in, GamepadEvent{.kind = GamepadKind::axis, .device = 1, .axis = GamepadAxis::left_x, .value = 0.5f}, area);
        const auto a = in.sample();
        expect(a.went_down(0) && a.axes[0] == -32767 && a.axes[1] == 16384) << std::format("{} {}", a.axes[0], a.axes[1]);
        meng::ui::feed(in, GamepadEvent{.kind = GamepadKind::up, .device = 1, .button = GamepadButton::dpad_left}, area);
        expect(eq(in.sample().axes[0], std::int16_t{0}));
    };
}};
