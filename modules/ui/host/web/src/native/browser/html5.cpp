// The page for openmui.host.web:platform: Emscripten's HTML5 API (its
// macros become the constants below, here and nowhere else) and the
// host's library, src/js/omui_web.js — the hidden textarea, the semantic
// DOM mirror, window.omui, the clipboard and localStorage are its; what they
// hear comes back through on_page(), in order with the canvas's events.
// Everything leaves as openmui's types.
module;
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
module openmui.host.web;

import std;
import openmui.core;
import openmui.raster;
import :platform;

extern "C" {
void omui_web_present(const char* selector, const std::uint8_t* pixels, int width, int height);
void omui_web_backing(const char* selector, int width, int height);
void omui_web_canvas(const char* selector, float* out);
int  omui_web_hidden();
int  omui_web_page();
void omui_web_title(const char* text);
char* omui_web_languages();
void omui_web_open(const char* selector, void (*on_page)(int kind, int a, int b, char* text));
int  omui_web_trusted();
void omui_web_text_input(float x, float y, float width, float height, const char* label);
void omui_web_mirror(const char* batch);
void omui_web_agent_reply(const char* line);
char* omui_web_clipboard();
void omui_web_set_clipboard(const char* text);
int  omui_web_open_url(const char* url);
int  omui_web_store();
char* omui_web_store_get(const char* key);
int  omui_web_store_set(const char* key, const char* value);
int  omui_web_store_remove(const char* key);
char* omui_web_store_keys(const char* prefix);
}

namespace omui::page {

namespace {

const char* const window_target = EMSCRIPTEN_EVENT_TARGET_WINDOW;
const pthread_t   calling_thread = EM_CALLBACK_THREAD_CONTEXT_CALLING_THREAD;
constexpr int     pixel_lines = DOM_DELTA_LINE, pixel_pages = DOM_DELTA_PAGE;
constexpr float   wheel_line = 40;  // logical pixels per line, as the desktop's notch

std::string               selector;  // the canvas's, alive for the callbacks
std::vector<InputEvent>   queue;     // what the page gave since the last take
std::vector<Request>      asked;     // what the hidden DOM and window.omui asked since
std::uint32_t             held = 0;  // mouse buttons held (bit i: button i + 1)

// KeyboardEvent.code (the key's place, whatever the layout says) to openmui's keys.
constexpr std::array<std::pair<std::string_view, Key>, 85> codes{{
    {"KeyA", Key::a}, {"KeyB", Key::b}, {"KeyC", Key::c}, {"KeyD", Key::d}, {"KeyE", Key::e}, {"KeyF", Key::f}, {"KeyG", Key::g},
    {"KeyH", Key::h}, {"KeyI", Key::i}, {"KeyJ", Key::j}, {"KeyK", Key::k}, {"KeyL", Key::l}, {"KeyM", Key::m}, {"KeyN", Key::n},
    {"KeyO", Key::o}, {"KeyP", Key::p}, {"KeyQ", Key::q}, {"KeyR", Key::r}, {"KeyS", Key::s}, {"KeyT", Key::t}, {"KeyU", Key::u},
    {"KeyV", Key::v}, {"KeyW", Key::w}, {"KeyX", Key::x}, {"KeyY", Key::y}, {"KeyZ", Key::z},
    {"Digit0", Key::digit0}, {"Digit1", Key::digit1}, {"Digit2", Key::digit2}, {"Digit3", Key::digit3}, {"Digit4", Key::digit4},
    {"Digit5", Key::digit5}, {"Digit6", Key::digit6}, {"Digit7", Key::digit7}, {"Digit8", Key::digit8}, {"Digit9", Key::digit9},
    {"F1", Key::f1}, {"F2", Key::f2}, {"F3", Key::f3}, {"F4", Key::f4}, {"F5", Key::f5}, {"F6", Key::f6},
    {"F7", Key::f7}, {"F8", Key::f8}, {"F9", Key::f9}, {"F10", Key::f10}, {"F11", Key::f11}, {"F12", Key::f12},
    {"Enter", Key::enter}, {"NumpadEnter", Key::enter}, {"Escape", Key::escape}, {"Backspace", Key::backspace}, {"Tab", Key::tab},
    {"Space", Key::space}, {"Minus", Key::minus}, {"Equal", Key::equal}, {"BracketLeft", Key::left_bracket},
    {"BracketRight", Key::right_bracket}, {"Backslash", Key::backslash}, {"Semicolon", Key::semicolon}, {"Quote", Key::apostrophe},
    {"Backquote", Key::grave}, {"Comma", Key::comma}, {"Period", Key::period}, {"Slash", Key::slash}, {"Insert", Key::insert},
    {"Delete", Key::delete_}, {"Home", Key::home}, {"End", Key::end}, {"PageUp", Key::page_up}, {"PageDown", Key::page_down},
    {"ArrowLeft", Key::left}, {"ArrowRight", Key::right}, {"ArrowUp", Key::up}, {"ArrowDown", Key::down},
    {"CapsLock", Key::caps_lock}, {"PrintScreen", Key::print_screen}, {"ScrollLock", Key::scroll_lock}, {"Pause", Key::pause},
    {"ContextMenu", Key::menu}, {"ShiftLeft", Key::shift}, {"ShiftRight", Key::shift}, {"ControlLeft", Key::control},
    {"ControlRight", Key::control}, {"AltLeft", Key::alt},
}};

Key key_of(std::string_view code) {
    if (code == "AltRight") return Key::alt;
    if (code == "MetaLeft" || code == "MetaRight") return Key::meta;
    for (const auto& [c, k] : codes)
        if (c == code) return k;
    return Key::unknown;
}

Modifiers mods(bool shift, bool ctrl, bool alt, bool meta) {
    Modifiers m;
    if (shift) m = m | Modifier::shift;
    if (ctrl) m = m | Modifier::ctrl;
    if (alt) m = m | Modifier::alt;
    if (meta) m = m | Modifier::meta;
    return m;
}

MouseButton button_of(unsigned short b) {
    switch (b) {
        case 0: return MouseButton::left;
        case 1: return MouseButton::middle;
        case 2: return MouseButton::right;
        case 3: return MouseButton::back;
        case 4: return MouseButton::forward;
        default: return MouseButton::none;
    }
}

// MouseEvent.buttons (1 left, 2 right, 4 middle, …) as openmui's bits (bit i: button i + 1).
std::uint32_t buttons_of(unsigned short b) {
    std::uint32_t out = 0;
    if (b & 1) out |= 1u << 0;
    if (b & 4) out |= 1u << 2;
    if (b & 2) out |= 1u << 1;
    if (b & 8) out |= 1u << 3;
    if (b & 16) out |= 1u << 4;
    return out;
}

// What the page's library says (src/js/omui_web.js: the same numbers).
constexpr int page_text = 1, page_preedit = 2, page_key = 3, page_activate = 4, page_focus = 5, page_agent = 6;
// KeyboardEvent.keyCode while an input method composes.
constexpr unsigned composing = 229;

// Only the person's own input is input (openas §2.7): an event a script of
// the page made (isTrusted false) is not, and is left to the page.
bool trusted() { return omui_web_trusted() != 0; }

bool on_key(int type, const EmscriptenKeyboardEvent* e, void*) {
    if (!trusted()) return false;
    // An input method composing, a dead key, a phone's keyboard: the text
    // comes from the hidden textarea, and the key stays the page's.
    if (const std::string_view key = e->key; e->keyCode == composing || key == "Process" || key == "Dead" || key == "Unidentified")
        return false;
    KeyEvent k;
    k.kind = type == EMSCRIPTEN_EVENT_KEYDOWN ? KeyKind::down : KeyKind::up;
    k.key = key_of(e->code);
    k.mods = mods(e->shiftKey, e->ctrlKey, e->altKey, e->metaKey);
    k.repeat = e->repeat;
    if (k.key != Key::unknown) queue.push_back(k);
    // Text as typed: one character, not a chord (an input method's text is
    // the hidden textarea's).
    const std::string_view key = e->key;
    const bool chord = e->ctrlKey || e->metaKey;
    if (k.kind == KeyKind::down && !chord && !key.empty() && key.size() <= 4 &&
        (key.size() == 1 ? static_cast<unsigned char>(key[0]) >= 0x20 : (static_cast<unsigned char>(key[0]) & 0xc0) == 0xc0))
        queue.push_back(TextEvent{std::string(key)});
    // The page keeps its shortcuts (reload, copy); the rest are the program's (no scrolling by arrows or space).
    return !chord;
}

bool on_mouse(int type, const EmscriptenMouseEvent* e, void*) {
    if (!trusted()) return false;
    PointerEvent p;
    p.position = {static_cast<float>(e->targetX), static_cast<float>(e->targetY)};
    p.mods = mods(e->shiftKey, e->ctrlKey, e->altKey, e->metaKey);
    p.buttons = buttons_of(e->buttons);
    switch (type) {
        case EMSCRIPTEN_EVENT_MOUSEDOWN: p.kind = PointerKind::down, p.button = button_of(e->button); break;
        case EMSCRIPTEN_EVENT_MOUSEUP: p.kind = PointerKind::up, p.button = button_of(e->button); break;
        case EMSCRIPTEN_EVENT_MOUSEENTER: p.kind = PointerKind::enter; break;
        case EMSCRIPTEN_EVENT_MOUSELEAVE: p.kind = PointerKind::leave; break;
        default: p.kind = PointerKind::move; break;
    }
    held = p.buttons;
    queue.push_back(p);
    return type == EMSCRIPTEN_EVENT_MOUSEDOWN;  // no text selection, no focus moving off
}

bool on_wheel(int, const EmscriptenWheelEvent* e, void*) {
    if (!trusted()) return false;
    WheelEvent w;
    w.position = {static_cast<float>(e->mouse.targetX), static_cast<float>(e->mouse.targetY)};
    const float unit = e->deltaMode == static_cast<unsigned>(pixel_lines) ? wheel_line
                     : e->deltaMode == static_cast<unsigned>(pixel_pages) ? wheel_line * 10
                                                                          : 1.0f;
    // The page's delta is how far the content goes down; openmui's positive y scrolls content up.
    w.delta = {static_cast<float>(e->deltaX) * unit, static_cast<float>(e->deltaY) * unit};
    w.precise = e->deltaMode == 0;
    w.mods = mods(e->mouse.shiftKey, e->mouse.ctrlKey, e->mouse.altKey, e->mouse.metaKey);
    queue.push_back(w);
    return true;
}

bool on_touch(int type, const EmscriptenTouchEvent* e, void*) {
    if (!trusted()) return false;
    for (int i = 0; i < e->numTouches && i < 32; ++i) {
        const EmscriptenTouchPoint& t = e->touches[i];
        if (!t.isChanged) continue;
        PointerEvent p;
        p.type = PointerType::touch;
        p.pointer = static_cast<std::uint32_t>(t.identifier) + 1;
        p.position = {static_cast<float>(t.targetX), static_cast<float>(t.targetY)};
        p.button = MouseButton::left;
        p.kind = type == EMSCRIPTEN_EVENT_TOUCHSTART ? PointerKind::down
               : type == EMSCRIPTEN_EVENT_TOUCHEND   ? PointerKind::up
               : type == EMSCRIPTEN_EVENT_TOUCHCANCEL ? PointerKind::cancel
                                                      : PointerKind::move;
        queue.push_back(p);
    }
    return true;  // no scrolling, no zooming, no mouse events made up from touches
}

// What the page's library heard: the hidden textarea's text and composition
// (offsets in bytes), a key a phone's keyboard gave only as an edit, and
// what the hidden DOM and window.omui ask. The text is the caller's to free.
void on_page(int kind, int a, int b, char* text) {
    std::string t = text ? text : "";
    std::free(text);
    switch (kind) {
        case page_text: if (!t.empty()) queue.push_back(TextEvent{std::move(t)}); break;
        case page_preedit: queue.push_back(PreeditEvent{std::move(t), a, b}); break;
        case page_key:
            if (const Key k = key_of(t); k != Key::unknown) {
                queue.push_back(KeyEvent{.kind = KeyKind::down, .key = k});
                queue.push_back(KeyEvent{.kind = KeyKind::up, .key = k});
            }
            break;
        case page_activate: asked.push_back({Request::Kind::activate, std::move(t)}); break;
        case page_focus: asked.push_back({Request::Kind::focus, std::move(t)}); break;
        case page_agent: asked.push_back({Request::Kind::agent, std::move(t)}); break;
        default: break;
    }
}

std::string taken(char* text) {
    std::string out = text ? text : "";
    std::free(text);
    return out;
}

// ── gamepads: the Gamepad API's standard mapping, sampled once a frame ──

// Standard-mapping button i (w3c Gamepad §"Remapping") as openmui's; the
// triggers (6, 7) are read as axes.
constexpr std::array<std::optional<GamepadButton>, 17> standard{{
    GamepadButton::south, GamepadButton::east, GamepadButton::west, GamepadButton::north,
    GamepadButton::left_shoulder, GamepadButton::right_shoulder, std::nullopt, std::nullopt,
    GamepadButton::back, GamepadButton::start, GamepadButton::left_stick, GamepadButton::right_stick,
    GamepadButton::dpad_up, GamepadButton::dpad_down, GamepadButton::dpad_left, GamepadButton::dpad_right,
    GamepadButton::guide,
}};

struct Slot {
    bool          connected = false;
    std::uint32_t device = 0;
};
std::array<Slot, 8> slots;  // by the browser's index (which it reuses)
std::uint32_t       next_device = 1;
GamepadReader       pads;

void sample_gamepads(std::vector<InputEvent>& out) {
    if (emscripten_sample_gamepad_data() != EMSCRIPTEN_RESULT_SUCCESS) return;
    const int n = std::min(emscripten_get_num_gamepads(), static_cast<int>(slots.size()));
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        Slot& s = slots[static_cast<std::size_t>(i)];
        EmscriptenGamepadEvent g{};
        const bool there = i < n && emscripten_get_gamepad_status(i, &g) == EMSCRIPTEN_RESULT_SUCCESS && g.connected;
        if (!there) {
            if (s.connected) pads.disconnect(s.device, out);
            s = {};
            continue;
        }
        if (!s.connected) s = {true, next_device++};  // a new connection: a new device (spec G-1)
        std::array<bool, gamepad_buttons> buttons{};
        for (int b = 0; b < g.numButtons && b < static_cast<int>(standard.size()); ++b)
            if (const auto mine = standard[static_cast<std::size_t>(b)]) buttons[static_cast<std::size_t>(*mine)] = g.digitalButton[b];
        std::array<float, gamepad_axes> axes{};
        for (int a = 0; a < 4 && a < g.numAxes; ++a) axes[static_cast<std::size_t>(a)] = static_cast<float>(g.axis[a]);
        if (g.numButtons > 7) {
            axes[static_cast<std::size_t>(GamepadAxis::left_trigger)] = static_cast<float>(g.analogButton[6]);
            axes[static_cast<std::size_t>(GamepadAxis::right_trigger)] = static_cast<float>(g.analogButton[7]);
        }
        pads.update(s.device, g.id, buttons, axes, out);
    }
}

}  // namespace

bool exists() { return omui_web_page() != 0; }

bool open(std::string_view canvas, std::string_view title) {
    if (!exists()) return false;
    selector = std::string(canvas);
    const char* c = selector.c_str();
    // The library's listeners first: whether an event is the person's is
    // known before the canvas's and the keys' callbacks below hear it.
    omui_web_open(c, &on_page);
    emscripten_set_keydown_callback_on_thread(window_target, nullptr, true, on_key, calling_thread);
    emscripten_set_keyup_callback_on_thread(window_target, nullptr, true, on_key, calling_thread);
    emscripten_set_mousedown_callback_on_thread(c, nullptr, true, on_mouse, calling_thread);
    emscripten_set_mouseup_callback_on_thread(c, nullptr, true, on_mouse, calling_thread);
    emscripten_set_mousemove_callback_on_thread(c, nullptr, true, on_mouse, calling_thread);
    emscripten_set_mouseenter_callback_on_thread(c, nullptr, true, on_mouse, calling_thread);
    emscripten_set_mouseleave_callback_on_thread(c, nullptr, true, on_mouse, calling_thread);
    emscripten_set_wheel_callback_on_thread(c, nullptr, true, on_wheel, calling_thread);
    emscripten_set_touchstart_callback_on_thread(c, nullptr, true, on_touch, calling_thread);
    emscripten_set_touchend_callback_on_thread(c, nullptr, true, on_touch, calling_thread);
    emscripten_set_touchmove_callback_on_thread(c, nullptr, true, on_touch, calling_thread);
    emscripten_set_touchcancel_callback_on_thread(c, nullptr, true, on_touch, calling_thread);
    if (!title.empty()) omui_web_title(std::string(title).c_str());
    return true;
}

Canvas canvas(std::string_view sel) {
    std::array<float, 3> v{};
    omui_web_canvas(std::string(sel).c_str(), v.data());
    return {{v[0], v[1]}, v[2] > 0 ? v[2] : 1.0f};
}

void take(std::vector<InputEvent>& out, std::vector<Request>& requests) {
    out.insert(out.end(), std::make_move_iterator(queue.begin()), std::make_move_iterator(queue.end()));
    queue.clear();
    requests.insert(requests.end(), std::make_move_iterator(asked.begin()), std::make_move_iterator(asked.end()));
    asked.clear();
    sample_gamepads(out);
}

void text_input(std::optional<Rect> caret, std::string_view label) {
    const std::string l(label);
    if (!caret) omui_web_text_input(0, 0, -1, -1, l.c_str());
    else omui_web_text_input(caret->x, caret->y, caret->width, caret->height, l.c_str());
}

void mirror(std::string_view batch) { omui_web_mirror(std::string(batch).c_str()); }

void agent_reply(std::string_view line) { omui_web_agent_reply(std::string(line).c_str()); }

std::string clipboard() { return taken(omui_web_clipboard()); }

void set_clipboard(std::string_view text) { omui_web_set_clipboard(std::string(text).c_str()); }

bool open_url(std::string_view url) { return omui_web_open_url(std::string(url).c_str()) != 0; }

bool storage() { return omui_web_store() != 0; }

std::optional<std::string> storage_get(std::string_view key) {
    char* v = omui_web_store_get(std::string(key).c_str());
    if (!v) return std::nullopt;
    return taken(v);
}

bool storage_set(std::string_view key, std::string_view value) {
    return omui_web_store_set(std::string(key).c_str(), std::string(value).c_str()) != 0;
}

bool storage_remove(std::string_view key) { return omui_web_store_remove(std::string(key).c_str()) != 0; }

std::vector<std::string> storage_keys(std::string_view prefix) {
    std::vector<std::string> out;
    const auto list = parse_json(taken(omui_web_store_keys(std::string(prefix).c_str())));
    if (list && list->list())
        for (const Value& k : *list->list()) out.emplace_back(k.as_string());
    return out;
}

void backing(std::string_view sel, int width, int height) { omui_web_backing(std::string(sel).c_str(), width, height); }

void present(std::string_view sel, const raster::Surface& frame) {
    omui_web_present(std::string(sel).c_str(), reinterpret_cast<const std::uint8_t*>(frame.row(0)), frame.width(), frame.height());
}

bool hidden() { return omui_web_hidden() != 0; }

bool loop(void (*tick)(void*), void* user) {
    // 0: at the display's rate (requestAnimationFrame); true: main does not return.
    emscripten_set_main_loop_arg(tick, user, 0, true);
    return true;
}

std::vector<std::string> locales() {
    std::vector<std::string> out;
    char* text = omui_web_languages();
    if (!text) return out;
    std::string_view s = text;
    while (!s.empty()) {
        const auto comma = s.find(',');
        if (comma != 0) out.emplace_back(s.substr(0, comma));
        if (comma == std::string_view::npos) break;
        s.remove_prefix(comma + 1);
    }
    std::free(text);
    return out;
}

}  // namespace omui::page
