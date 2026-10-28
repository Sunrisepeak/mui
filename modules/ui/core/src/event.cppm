// openmui.core:event — input as the host delivers it (spec §6).
//
// One frame's input is a list of these. They have a JSON form, which is how
// a headless host, the C ABI (OmuiFrameInput.events_json) and session
// recordings carry them: a recorded session replays the same events at the
// same clock times and produces the same tree.
export module openmui.core:event;

import std;
import :base;
import :keys;

export namespace omui {

enum class PointerKind : std::uint8_t { down, up, move, enter, leave, cancel };
enum class PointerType : std::uint8_t { mouse, touch, pen };
enum class MouseButton : std::uint8_t { none, left, right, middle, back, forward };

struct PointerEvent {
    PointerKind   kind = PointerKind::move;
    Point         position;            // logical pixels, window space
    std::uint32_t pointer = 0;         // which pointer (touches differ)
    PointerType   type = PointerType::mouse;
    MouseButton   button = MouseButton::none;  // the button that changed (down/up)
    std::uint32_t buttons = 0;         // bit i: button i+1 held
    Modifiers     mods;
    std::uint8_t  clicks = 1;          // 2 for a double click
    float         pressure = 0.5f;
};

struct WheelEvent {
    Point     position;
    Point     delta;           // logical pixels; positive y scrolls content up
    bool      precise = false; // a trackpad or a high-resolution wheel
    Modifiers mods;
};

enum class KeyKind : std::uint8_t { down, up };

struct KeyEvent {
    KeyKind   kind = KeyKind::down;
    Key       key = Key::unknown;
    Modifiers mods;
    bool      repeat = false;
};

// Text committed by the keyboard or an input method.
struct TextEvent {
    std::string text;
};

// An input method's pre-edit (composition) text; empty ends the composition.
struct PreeditEvent {
    std::string text;
    std::int32_t cursor_begin = -1;  // byte offsets into text, -1 when hidden
    std::int32_t cursor_end   = -1;
};

struct FocusEvent {
    bool gained = true;  // the window gained or lost keyboard focus
};

struct ResizeEvent {
    Size  size;           // logical pixels
    float scale = 1.0f;   // physical pixels per logical pixel
};

struct CloseRequestEvent {};

using InputEvent = std::variant<PointerEvent, WheelEvent, KeyEvent, TextEvent, PreeditEvent, FocusEvent, ResizeEvent,
                                CloseRequestEvent>;

// {"type": "pointer", "kind": "down", "x": 10, "y": 20, "button": "left", …}
[[nodiscard]] Value to_value(const InputEvent& e);
[[nodiscard]] std::optional<InputEvent> event_from_value(const Value& v);

}  // namespace omui
