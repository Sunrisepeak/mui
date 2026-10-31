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

// ── gamepads (spec §6.1) ──
//
// The standard set, by position (south is A on an Xbox pad, Cross on a
// PlayStation one): what SDL's mapping database and the browser's standard
// Gamepad mapping both reduce a device to.
enum class GamepadButton : std::uint8_t {
    south, east, west, north,
    back, guide, start,
    left_stick, right_stick,        // the sticks pressed in
    left_shoulder, right_shoulder,  // L1 R1
    left_trigger, right_trigger,    // L2 R2, as buttons (also axes)
    dpad_up, dpad_down, dpad_left, dpad_right,
    count_,
};
// Sticks −1 … 1 (y positive down, as the screen), triggers 0 … 1.
enum class GamepadAxis : std::uint8_t { left_x, left_y, right_x, right_y, left_trigger, right_trigger, count_ };
enum class GamepadKind : std::uint8_t { connected, disconnected, down, up, axis };

inline constexpr std::size_t gamepad_buttons = static_cast<std::size_t>(GamepadButton::count_);
inline constexpr std::size_t gamepad_axes = static_cast<std::size_t>(GamepadAxis::count_);

struct GamepadEvent {
    GamepadKind   kind = GamepadKind::down;
    std::uint32_t device = 0;      // one connection of one device: never reused while the program runs
    GamepadButton button = GamepadButton::south;  // down / up
    GamepadAxis   axis = GamepadAxis::left_x;     // axis
    float         value = 0;       // axis: after the host's dead zone, in steps of 1/256
    std::string   name;            // connected: what the device calls itself
};

using InputEvent = std::variant<PointerEvent, WheelEvent, KeyEvent, TextEvent, PreeditEvent, FocusEvent, ResizeEvent,
                                CloseRequestEvent, GamepadEvent>;

[[nodiscard]] std::string_view name(GamepadButton b) noexcept;  // "south", "leftShoulder", "dpadUp"
[[nodiscard]] std::string_view name(GamepadAxis a) noexcept;    // "leftX", "rightTrigger"
[[nodiscard]] std::optional<GamepadButton> parse_gamepad_button(std::string_view text) noexcept;
[[nodiscard]] std::optional<GamepadAxis> parse_gamepad_axis(std::string_view text) noexcept;

// What a host makes of a device's raw readings (spec §6.1): the sticks'
// radial dead zone and the triggers' applied, values in steps of 1/256,
// nothing repeated; a trigger pulled past half is its button too; a device
// that goes releases what it held first. Event-driven devices (SDL) give
// each change; polled ones (the browser's Gamepad API) give their whole
// state once a frame.
class GamepadReader {
public:
    static constexpr float stick_dead_zone = 0.24f;    // SDL's suggested 8000 / 32767
    static constexpr float trigger_dead_zone = 0.06f;
    static constexpr float trigger_press = 0.5f, trigger_release = 0.4f;

    void connect(std::uint32_t device, std::string_view name, std::vector<InputEvent>& out);
    void disconnect(std::uint32_t device, std::vector<InputEvent>& out);
    void button(std::uint32_t device, GamepadButton b, bool down, std::vector<InputEvent>& out);
    // raw: sticks −1 … 1 (y down), triggers 0 … 1.
    void axis(std::uint32_t device, GamepadAxis a, float raw, std::vector<InputEvent>& out);
    // A polled device's whole state; connects it when it is new.
    void update(std::uint32_t device, std::string_view name, const std::array<bool, gamepad_buttons>& buttons,
                const std::array<float, gamepad_axes>& axes, std::vector<InputEvent>& out);
    [[nodiscard]] bool connected(std::uint32_t device) const noexcept;
    [[nodiscard]] std::vector<std::uint32_t> devices() const;

private:
    struct Device {
        std::uint32_t                         id = 0;
        std::array<bool, gamepad_buttons>     held{};
        std::array<float, gamepad_axes>       raw{}, value{};
    };
    Device* find(std::uint32_t device) noexcept;
    void emit_axis(Device& d, GamepadAxis a, float v, std::vector<InputEvent>& out);
    std::vector<Device> devices_;
};

// {"type": "pointer", "kind": "down", "x": 10, "y": 20, "button": "left", …}
// {"type": "gamepad", "kind": "down", "device": 1, "button": "south"}
[[nodiscard]] Value to_value(const InputEvent& e);
[[nodiscard]] std::optional<InputEvent> event_from_value(const Value& v);

}  // namespace omui
