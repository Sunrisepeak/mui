import std;
import omui.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"core.input", [] {

    "keys and chords"_test = [] {
        expect(omui::parse_key("PageUp") == omui::Key::page_up);
        expect(omui::parse_key("pageup") == omui::Key::page_up);
        expect(omui::parse_key("Esc") == omui::Key::escape);
        auto c = omui::parse_chord("Ctrl+Shift+K");
        expect(c && c->key == omui::Key::k && c->mods == (omui::Modifier::ctrl | omui::Modifier::shift));
        expect(eq(c->text(), std::string("Ctrl+Shift+K")));
        auto alt = omui::parse_chords("Right|Up");
        expect(alt && alt->size() == 2 && (*alt)[1].key == omui::Key::up);
        expect(!omui::parse_chord("Hyper+K").has_value());
        expect(!omui::parse_chords("Right|").has_value());
        omui::set_primary_modifier(omui::Modifier::meta);
        expect(omui::parse_chord("Mod+S")->mods == omui::Modifiers(omui::Modifier::meta));
        omui::set_primary_modifier(omui::Modifier::ctrl);
        expect(omui::parse_chord("Mod+S")->mods == omui::Modifiers(omui::Modifier::ctrl));
    };

    "events round-trip through their JSON form"_test = [] {
        std::vector<omui::InputEvent> events{
            omui::PointerEvent{.kind = omui::PointerKind::down, .position = {10, 20}, .button = omui::MouseButton::left,
                               .buttons = 1, .mods = omui::Modifier::shift, .clicks = 2},
            omui::WheelEvent{.position = {1, 2}, .delta = {0, -40}, .precise = true},
            omui::KeyEvent{.kind = omui::KeyKind::down, .key = omui::Key::enter, .mods = omui::Modifier::ctrl, .repeat = true},
            omui::TextEvent{"你好"},
            omui::PreeditEvent{"ni", 0, 2},
            omui::FocusEvent{false},
            omui::ResizeEvent{{800, 600}, 2.0f},
            omui::CloseRequestEvent{},
            omui::GamepadEvent{.kind = omui::GamepadKind::connected, .device = 3, .name = "Xbox Series X Controller"},
            omui::GamepadEvent{.kind = omui::GamepadKind::down, .device = 3, .button = omui::GamepadButton::right_shoulder},
            omui::GamepadEvent{.kind = omui::GamepadKind::up, .device = 3, .button = omui::GamepadButton::dpad_left},
            omui::GamepadEvent{.kind = omui::GamepadKind::axis, .device = 3, .axis = omui::GamepadAxis::left_y, .value = -0.5f},
            omui::GamepadEvent{.kind = omui::GamepadKind::disconnected, .device = 3},
            omui::FileDropEvent{.kind = omui::FileDropEvent::Kind::over, .position = {40, 60}, .files = {}},
            omui::FileDropEvent{.kind = omui::FileDropEvent::Kind::drop, .position = {40, 60}, .files = {"/home/ada/猫.png", "C:\\notes.txt"}},
            omui::FileDropEvent{.kind = omui::FileDropEvent::Kind::leave, .position = {0, 0}, .files = {}},
        };
        for (const auto& e : events) {
            auto v = omui::to_value(e);
            auto back = omui::event_from_value(v);
            expect(back.has_value()) << v.json();
            expect(eq(omui::to_value(*back).json(), v.json()));
        }
        expect(!omui::event_from_value(omui::Value("x")).has_value());
    };

    "a gamepad's JSON form"_test = [] {
        auto v = omui::to_value(omui::GamepadEvent{.kind = omui::GamepadKind::down, .device = 1, .button = omui::GamepadButton::south});
        expect(eq(v.get("type")->as_string(), std::string_view("gamepad")));
        expect(eq(v.get("button")->as_string(), std::string_view("south")));
        expect(eq(v.get("device")->as_int(), std::int64_t{1}));
        expect(!omui::event_from_value(*omui::parse_json(R"({"type":"gamepad","kind":"down","button":"A"})")).has_value());
        expect(!omui::event_from_value(*omui::parse_json(R"({"type":"gamepad","kind":"pressed"})")).has_value());
        expect(omui::parse_gamepad_button("leftShoulder") == omui::GamepadButton::left_shoulder);
        expect(omui::parse_gamepad_axis("rightTrigger") == omui::GamepadAxis::right_trigger);
    };

    "the gamepad reader: dead zones, triggers as buttons, nothing repeated, a device going releases"_test = [] {
        using omui::GamepadAxis, omui::GamepadButton, omui::GamepadEvent, omui::GamepadKind;
        omui::GamepadReader r;
        std::vector<omui::InputEvent> out;
        auto only = [&](std::size_t n) {
            expect(eq(out.size(), n));
            std::vector<GamepadEvent> got;
            for (const auto& e : out) got.push_back(std::get<GamepadEvent>(e));
            out.clear();
            return got;
        };
        r.button(7, GamepadButton::south, true, out);  // not connected: nothing
        only(0);
        r.connect(7, "pad", out);
        r.connect(7, "pad", out);
        expect(only(1)[0].kind == GamepadKind::connected);
        r.button(7, GamepadButton::south, true, out);
        r.button(7, GamepadButton::south, true, out);
        expect(only(1)[0].button == GamepadButton::south);
        // Inside the stick's dead zone: still nothing; past it, stretched; a circle, not a square.
        r.axis(7, GamepadAxis::left_x, 0.2f, out);
        only(0);
        r.axis(7, GamepadAxis::left_x, 1.0f, out);
        auto x = only(1);
        expect(x[0].axis == GamepadAxis::left_x && x[0].value == 1.0f);
        r.axis(7, GamepadAxis::left_y, 0.05f, out);  // a little diagonal: x stays, y shows a little
        auto y = only(1);
        expect(y[0].axis == GamepadAxis::left_y && y[0].value > 0 && y[0].value < 0.2f) << std::format("{}", y[0].value);
        expect(eq(y[0].value * 256, std::round(y[0].value * 256)));  // steps of 1/256
        // A trigger past half is its button too; back below 0.4 lets it go.
        r.axis(7, GamepadAxis::right_trigger, 0.8f, out);
        auto t = only(2);
        expect(t[0].kind == GamepadKind::axis && t[1].kind == GamepadKind::down && t[1].button == GamepadButton::right_trigger);
        r.axis(7, GamepadAxis::right_trigger, 0.45f, out);
        expect(only(1)[0].kind == GamepadKind::axis);
        r.axis(7, GamepadAxis::right_trigger, 0.3f, out);
        expect(only(2)[1].kind == GamepadKind::up);
        // Going: the held button released, the stick at rest, then gone.
        r.disconnect(7, out);
        auto gone = only(5);  // south up; the stick's two axes and the trigger at rest; gone
        expect(gone[0].kind == GamepadKind::up && gone[0].button == GamepadButton::south);
        expect(gone[1].kind == GamepadKind::axis && gone[1].value == 0 && gone[2].kind == GamepadKind::axis);
        expect(gone[3].axis == GamepadAxis::right_trigger && gone[3].value == 0);
        expect(gone[4].kind == GamepadKind::disconnected);
        expect(!r.connected(7));
        // A polled device: its state at once, connected when new.
        std::array<bool, omui::gamepad_buttons> held{};
        std::array<float, omui::gamepad_axes> axes{};
        held[static_cast<std::size_t>(GamepadButton::dpad_up)] = true;
        r.update(9, "web", held, axes, out);
        auto polled = only(2);
        expect(polled[0].kind == GamepadKind::connected && polled[1].button == GamepadButton::dpad_up);
        r.update(9, "web", held, axes, out);
        only(0);
    };
}};
