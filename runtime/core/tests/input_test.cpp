import std;
import openmui.core;
import openmui.testing;

using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"core.input"};

    s.test("keys and chords", [] {
        check(omui::parse_key("PageUp") == omui::Key::page_up);
        check(omui::parse_key("pageup") == omui::Key::page_up);
        check(omui::parse_key("Esc") == omui::Key::escape);
        auto c = omui::parse_chord("Ctrl+Shift+K");
        check(c && c->key == omui::Key::k && c->mods == (omui::Modifier::ctrl | omui::Modifier::shift));
        check_eq(c->text(), std::string("Ctrl+Shift+K"));
        auto alt = omui::parse_chords("Right|Up");
        check(alt && alt->size() == 2 && (*alt)[1].key == omui::Key::up);
        check(!omui::parse_chord("Hyper+K").has_value());
        check(!omui::parse_chords("Right|").has_value());
        omui::set_primary_modifier(omui::Modifier::meta);
        check(omui::parse_chord("Mod+S")->mods == omui::Modifiers(omui::Modifier::meta));
        omui::set_primary_modifier(omui::Modifier::ctrl);
        check(omui::parse_chord("Mod+S")->mods == omui::Modifiers(omui::Modifier::ctrl));
    });

    s.test("events round-trip through their JSON form", [] {
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
        };
        for (const auto& e : events) {
            auto v = omui::to_value(e);
            auto back = omui::event_from_value(v);
            check(back.has_value(), v.json());
            check_eq(omui::to_value(*back).json(), v.json());
        }
        check(!omui::event_from_value(omui::Value("x")).has_value());
    });

    return s.run(argc, argv);
}
