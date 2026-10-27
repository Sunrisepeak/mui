// SDL3 for openmui.host.sdl3:platform. The one unit that sees SDL's headers
// (and its macros): everything leaves as openmui's own types.
module;
// The headers this unit uses, not <SDL3/SDL.h>: that one brings SDL_assert.h,
// whose <intrin.h> on MinGW puts libstdc++'s C++ wrappers in this global
// module fragment, where they conflict with `import std`.
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
module openmui.host.sdl3;

import std;
import openmui.core;
import openmui.raster;
import :platform;

namespace omui::platform {

namespace {

// SDL is initialised while any window is open.
int            windows_open = 0;
std::uint32_t  wake_event = 0;
std::atomic<bool> sdl_ready{false};

constexpr std::array<std::pair<SDL_Keycode, Key>, 82> keymap{{
    {SDLK_A, Key::a}, {SDLK_B, Key::b}, {SDLK_C, Key::c}, {SDLK_D, Key::d}, {SDLK_E, Key::e}, {SDLK_F, Key::f},
    {SDLK_G, Key::g}, {SDLK_H, Key::h}, {SDLK_I, Key::i}, {SDLK_J, Key::j}, {SDLK_K, Key::k}, {SDLK_L, Key::l},
    {SDLK_M, Key::m}, {SDLK_N, Key::n}, {SDLK_O, Key::o}, {SDLK_P, Key::p}, {SDLK_Q, Key::q}, {SDLK_R, Key::r},
    {SDLK_S, Key::s}, {SDLK_T, Key::t}, {SDLK_U, Key::u}, {SDLK_V, Key::v}, {SDLK_W, Key::w}, {SDLK_X, Key::x},
    {SDLK_Y, Key::y}, {SDLK_Z, Key::z},
    {SDLK_0, Key::digit0}, {SDLK_1, Key::digit1}, {SDLK_2, Key::digit2}, {SDLK_3, Key::digit3}, {SDLK_4, Key::digit4},
    {SDLK_5, Key::digit5}, {SDLK_6, Key::digit6}, {SDLK_7, Key::digit7}, {SDLK_8, Key::digit8}, {SDLK_9, Key::digit9},
    {SDLK_F1, Key::f1}, {SDLK_F2, Key::f2}, {SDLK_F3, Key::f3}, {SDLK_F4, Key::f4}, {SDLK_F5, Key::f5}, {SDLK_F6, Key::f6},
    {SDLK_F7, Key::f7}, {SDLK_F8, Key::f8}, {SDLK_F9, Key::f9}, {SDLK_F10, Key::f10}, {SDLK_F11, Key::f11}, {SDLK_F12, Key::f12},
    {SDLK_RETURN, Key::enter}, {SDLK_KP_ENTER, Key::enter}, {SDLK_ESCAPE, Key::escape}, {SDLK_BACKSPACE, Key::backspace},
    {SDLK_TAB, Key::tab}, {SDLK_SPACE, Key::space}, {SDLK_MINUS, Key::minus}, {SDLK_EQUALS, Key::equal},
    {SDLK_LEFTBRACKET, Key::left_bracket}, {SDLK_RIGHTBRACKET, Key::right_bracket}, {SDLK_BACKSLASH, Key::backslash},
    {SDLK_SEMICOLON, Key::semicolon}, {SDLK_APOSTROPHE, Key::apostrophe}, {SDLK_GRAVE, Key::grave}, {SDLK_COMMA, Key::comma},
    {SDLK_PERIOD, Key::period}, {SDLK_SLASH, Key::slash}, {SDLK_INSERT, Key::insert}, {SDLK_DELETE, Key::delete_},
    {SDLK_HOME, Key::home}, {SDLK_END, Key::end}, {SDLK_PAGEUP, Key::page_up}, {SDLK_PAGEDOWN, Key::page_down},
    {SDLK_LEFT, Key::left}, {SDLK_RIGHT, Key::right}, {SDLK_UP, Key::up}, {SDLK_DOWN, Key::down},
    {SDLK_CAPSLOCK, Key::caps_lock}, {SDLK_PRINTSCREEN, Key::print_screen}, {SDLK_SCROLLLOCK, Key::scroll_lock},
    {SDLK_PAUSE, Key::pause}, {SDLK_APPLICATION, Key::menu}, {SDLK_LSHIFT, Key::shift}, {SDLK_LCTRL, Key::control},
}};

Key key_of(SDL_Keycode k) {
    switch (k) {
        case SDLK_RSHIFT: return Key::shift;
        case SDLK_RCTRL: return Key::control;
        case SDLK_LALT:
        case SDLK_RALT: return Key::alt;
        case SDLK_LGUI:
        case SDLK_RGUI: return Key::meta;
        default: break;
    }
    for (const auto& [sdl, key] : keymap)
        if (sdl == k) return key;
    return Key::unknown;
}

SDL_Keycode keycode_of(Key k) {
    switch (k) {
        case Key::alt: return SDLK_LALT;
        case Key::meta: return SDLK_LGUI;
        default: break;
    }
    for (const auto& [sdl, key] : keymap)
        if (key == k) return sdl;
    return SDLK_UNKNOWN;
}

Modifiers mods_of(SDL_Keymod m) {
    Modifiers out;
    if (m & SDL_KMOD_SHIFT) out = out | Modifier::shift;
    if (m & SDL_KMOD_CTRL) out = out | Modifier::ctrl;
    if (m & SDL_KMOD_ALT) out = out | Modifier::alt;
    if (m & SDL_KMOD_GUI) out = out | Modifier::meta;
    return out;
}

SDL_Keymod keymod_of(Modifiers m) {
    SDL_Keymod out = SDL_KMOD_NONE;
    if (m.has(Modifier::shift)) out |= SDL_KMOD_LSHIFT;
    if (m.has(Modifier::ctrl)) out |= SDL_KMOD_LCTRL;
    if (m.has(Modifier::alt)) out |= SDL_KMOD_LALT;
    if (m.has(Modifier::meta)) out |= SDL_KMOD_LGUI;
    return out;
}

MouseButton button_of(std::uint8_t b) {
    switch (b) {
        case SDL_BUTTON_LEFT: return MouseButton::left;
        case SDL_BUTTON_RIGHT: return MouseButton::right;
        case SDL_BUTTON_MIDDLE: return MouseButton::middle;
        case SDL_BUTTON_X1: return MouseButton::back;
        case SDL_BUTTON_X2: return MouseButton::forward;
        default: return MouseButton::none;
    }
}

std::uint8_t sdl_button_of(MouseButton b) {
    switch (b) {
        case MouseButton::left: return SDL_BUTTON_LEFT;
        case MouseButton::right: return SDL_BUTTON_RIGHT;
        case MouseButton::middle: return SDL_BUTTON_MIDDLE;
        case MouseButton::back: return SDL_BUTTON_X1;
        case MouseButton::forward: return SDL_BUTTON_X2;
        case MouseButton::none: break;
    }
    return 0;
}

std::uint32_t buttons_of(SDL_MouseButtonFlags state) {
    std::uint32_t out = 0;
    for (int b = 1; b <= 5; ++b)
        if (state & SDL_BUTTON_MASK(b)) out |= 1u << (b - 1);
    return out;
}

constexpr float wheel_line = 40;  // logical pixels per wheel notch

}  // namespace

struct Window {
    SDL_Window*             window = nullptr;
    SDL_Renderer*           renderer = nullptr;
    SDL_Texture*            texture = nullptr;
    int                     tw = 0, th = 0;
    std::unordered_map<std::string, SDL_Cursor*> cursors;
    std::string             shape = "default";
    bool                    typing = false;
    std::deque<std::string> synthesized_text;  // text of synthesized events, alive until polled

    // Window coordinates → logical pixels.
    [[nodiscard]] float to_logical() const {
        const float density = SDL_GetWindowPixelDensity(window);
        const float scale = SDL_GetWindowDisplayScale(window);
        return scale > 0 ? density / scale : 1.0f;
    }
};

void WindowDeleter::operator()(Window* w) const noexcept {
    if (!w) return;
    for (auto& [name, c] : w->cursors) SDL_DestroyCursor(c);
    if (w->texture) SDL_DestroyTexture(w->texture);
    if (w->renderer) SDL_DestroyRenderer(w->renderer);
    if (w->window) SDL_DestroyWindow(w->window);
    delete w;
    if (--windows_open == 0) {
        sdl_ready = false;
        SDL_Quit();
    }
}

std::expected<WindowPtr, std::string> open(const WindowOptions& o) {
    if (windows_open == 0) {
        if (!o.video_driver.empty()) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, o.video_driver.c_str());
        SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");  // openmui draws the preedit itself
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return std::unexpected(std::format("SDL_Init: {}", SDL_GetError()));
        wake_event = SDL_RegisterEvents(1);
        sdl_ready = true;
    }
    ++windows_open;
    WindowPtr w(new Window);
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (o.resizable) flags |= SDL_WINDOW_RESIZABLE;
    // The software renderer draws into the window's surface and needs no GPU
    // context; saying so stops SDL on macOS from implying OpenGL for any
    // driver that has it — the offscreen driver's is EGL, which macOS lacks.
    const bool software = !o.gpu && o.render_driver == "software";
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, o.title.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, static_cast<Sint64>(o.size.width));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, static_cast<Sint64>(o.size.height));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
    // The window's graphics are someone else's: the software renderer draws
    // into its surface, and a GPU device claims it (Metal views, Vulkan
    // surfaces) — so SDL loads no default backend (OpenGL on macOS) for it.
    if (software || o.gpu) SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
    w->window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!w->window) return std::unexpected(std::format("SDL_CreateWindow: {}", SDL_GetError()));
    // A software renderer draws into the window's framebuffer, which SDL by
    // default backs with an accelerated renderer — loading the GPU drivers
    // (EGL, GL, Vulkan) for a window that asked for none. Kept in memory.
    if (software) SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    if (o.gpu) return w;  // the GPU device claims it and presents
    w->renderer = SDL_CreateRenderer(w->window, o.render_driver.empty() ? nullptr : o.render_driver.c_str());
    if (!w->renderer) return std::unexpected(std::format("SDL_CreateRenderer: {}", SDL_GetError()));
    if (o.vsync) SDL_SetRenderVSync(w->renderer, 1);
    return w;
}

std::uint32_t window_id(Window& w) { return static_cast<std::uint32_t>(SDL_GetWindowID(w.window)); }

Size logical_size(Window& w) {
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(w.window, &pw, &ph);
    const float s = pixel_scale(w);
    return {static_cast<float>(pw) / s, static_cast<float>(ph) / s};
}

float pixel_scale(Window& w) {
    const float s = SDL_GetWindowDisplayScale(w.window);
    return s > 0 ? s : 1.0f;
}

Polled poll(Window& w, bool wait, std::optional<std::chrono::milliseconds> timeout) {
    Polled out;
    SDL_Event e;
    bool have = false;
    if (wait) {
        if (timeout) have = SDL_WaitEventTimeout(&e, static_cast<std::int32_t>(std::clamp<std::int64_t>(timeout->count(), 0, 1 << 30)));
        else have = SDL_WaitEvent(&e);
    } else {
        have = SDL_PollEvent(&e);
    }
    const float k = w.to_logical();
    auto point = [&](float x, float y) { return Point{x * k, y * k}; };
    while (have) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                out.events.push_back(CloseRequestEvent{});
                out.quit = true;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_RESIZED:
                out.events.push_back(ResizeEvent{logical_size(w), pixel_scale(w)});
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED: out.events.push_back(FocusEvent{true}); break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: out.events.push_back(FocusEvent{false}); break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
                PointerEvent p;
                p.kind = PointerKind::leave;
                out.events.push_back(p);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                if (e.motion.which == SDL_TOUCH_MOUSEID) break;  // touches arrive as fingers
                PointerEvent p;
                p.kind = PointerKind::move;
                p.position = point(e.motion.x, e.motion.y);
                p.buttons = buttons_of(e.motion.state);
                p.mods = mods_of(SDL_GetModState());
                out.events.push_back(p);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (e.button.which == SDL_TOUCH_MOUSEID) break;
                PointerEvent p;
                p.kind = e.button.down ? PointerKind::down : PointerKind::up;
                p.position = point(e.button.x, e.button.y);
                p.button = button_of(e.button.button);
                p.buttons = buttons_of(SDL_GetMouseState(nullptr, nullptr));
                p.clicks = e.button.clicks;
                p.mods = mods_of(SDL_GetModState());
                out.events.push_back(p);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                WheelEvent wv;
                const float flip = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                wv.position = point(e.wheel.mouse_x, e.wheel.mouse_y);
                // A notch is a line; a trackpad's fractions are pixels already.
                wv.precise = e.wheel.x != static_cast<float>(e.wheel.integer_x) || e.wheel.y != static_cast<float>(e.wheel.integer_y);
                const float unit = wv.precise ? 1.0f : wheel_line;
                wv.delta = {e.wheel.x * flip * unit, -e.wheel.y * flip * unit};
                wv.mods = mods_of(SDL_GetModState());
                out.events.push_back(wv);
                break;
            }
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_CANCELED: {
                PointerEvent p;
                p.type = PointerType::touch;
                p.kind = e.type == SDL_EVENT_FINGER_DOWN ? PointerKind::down
                       : e.type == SDL_EVENT_FINGER_UP ? PointerKind::up
                       : e.type == SDL_EVENT_FINGER_CANCELED ? PointerKind::cancel
                                                             : PointerKind::move;
                const Size s = logical_size(w);
                p.position = {e.tfinger.x * s.width, e.tfinger.y * s.height};
                p.pointer = static_cast<std::uint32_t>(e.tfinger.fingerID);
                p.pressure = e.tfinger.pressure;
                p.button = MouseButton::left;
                out.events.push_back(p);
                break;
            }
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                KeyEvent k;
                k.kind = e.key.down ? KeyKind::down : KeyKind::up;
                k.key = key_of(e.key.key);
                k.mods = mods_of(e.key.mod);
                k.repeat = e.key.repeat;
                if (k.key != Key::unknown) out.events.push_back(k);
                break;
            }
            case SDL_EVENT_TEXT_INPUT:
                if (e.text.text && *e.text.text) out.events.push_back(TextEvent{e.text.text});
                break;
            case SDL_EVENT_TEXT_EDITING: {
                PreeditEvent pe;
                pe.text = e.edit.text ? e.edit.text : "";
                if (e.edit.start >= 0) {
                    // SDL counts characters; openmui counts bytes.
                    auto byte_at = [&](std::int32_t chars) {
                        std::size_t i = 0;
                        for (std::int32_t c = 0; c < chars && i < pe.text.size(); ++c) {
                            ++i;
                            while (i < pe.text.size() && (static_cast<unsigned char>(pe.text[i]) & 0xc0) == 0x80) ++i;
                        }
                        return static_cast<std::int32_t>(i);
                    };
                    pe.cursor_begin = byte_at(e.edit.start);
                    pe.cursor_end = byte_at(e.edit.start + std::max(0, e.edit.length));
                }
                out.events.push_back(pe);
                break;
            }
            default:
                break;  // the wake event, and what openmui does not use
        }
        have = SDL_PollEvent(&e);
    }
    w.synthesized_text.clear();
    return out;
}

void present(Window& w, const raster::Surface& frame) {
    if (frame.width() <= 0 || frame.height() <= 0 || !w.renderer) return;
    if (!w.texture || w.tw != frame.width() || w.th != frame.height()) {
        if (w.texture) SDL_DestroyTexture(w.texture);
        // RGBA32 is R, G, B, A in memory on every platform: the surface's bytes.
        w.texture = SDL_CreateTexture(w.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, frame.width(), frame.height());
        if (!w.texture) return;
        SDL_SetTextureBlendMode(w.texture, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(w.texture, SDL_SCALEMODE_NEAREST);
        w.tw = frame.width();
        w.th = frame.height();
    }
    SDL_UpdateTexture(w.texture, nullptr, frame.row(0), frame.width() * 4);
    SDL_RenderClear(w.renderer);
    SDL_RenderTexture(w.renderer, w.texture, nullptr, nullptr);
    SDL_RenderPresent(w.renderer);
}

void text_input(Window& w, std::optional<Rect> caret) {
    if (!caret) {
        if (w.typing) SDL_StopTextInput(w.window);
        w.typing = false;
        return;
    }
    const float k = w.to_logical();
    SDL_Rect r{static_cast<int>(caret->x / k), static_cast<int>(caret->y / k), std::max(1, static_cast<int>(caret->width / k)),
               std::max(1, static_cast<int>(caret->height / k))};
    SDL_SetTextInputArea(w.window, &r, 0);
    if (!w.typing) SDL_StartTextInput(w.window);
    w.typing = true;
}

void cursor(Window& w, std::string_view shape) {
    if (shape == w.shape) return;
    w.shape = std::string(shape);
    SDL_SystemCursor id = SDL_SYSTEM_CURSOR_DEFAULT;
    if (shape == "text") id = SDL_SYSTEM_CURSOR_TEXT;
    else if (shape == "pointer") id = SDL_SYSTEM_CURSOR_POINTER;
    else if (shape == "grab" || shape == "move") id = SDL_SYSTEM_CURSOR_MOVE;
    else if (shape == "not_allowed") id = SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    else if (shape == "ew_resize") id = SDL_SYSTEM_CURSOR_EW_RESIZE;
    else if (shape == "ns_resize") id = SDL_SYSTEM_CURSOR_NS_RESIZE;
    else if (shape == "crosshair") id = SDL_SYSTEM_CURSOR_CROSSHAIR;
    else if (shape == "wait") id = SDL_SYSTEM_CURSOR_WAIT;
    auto it = w.cursors.find(w.shape);
    if (it == w.cursors.end()) it = w.cursors.emplace(w.shape, SDL_CreateSystemCursor(id)).first;
    if (it->second) SDL_SetCursor(it->second);
}

std::string clipboard() {
    char* t = SDL_GetClipboardText();
    std::string out = t ? t : "";
    SDL_free(t);
    return out;
}

void set_clipboard(std::string_view text) { SDL_SetClipboardText(std::string(text).c_str()); }

void wake() {
    if (!sdl_ready) return;
    SDL_Event e{};
    e.type = wake_event;
    SDL_PushEvent(&e);
}

void synthesize(Window& w, std::span<const InputEvent> events) {
    const float k = w.to_logical();
    const SDL_WindowID id = SDL_GetWindowID(w.window);
    for (const InputEvent& ev : events) {
        SDL_Event e{};
        bool push = true;
        std::visit(
            [&](const auto& x) {
                using T = std::remove_cvref_t<decltype(x)>;
                if constexpr (std::same_as<T, PointerEvent>) {
                    if (x.kind == PointerKind::move) {
                        e.type = SDL_EVENT_MOUSE_MOTION;
                        e.motion.windowID = id;
                        e.motion.x = x.position.x / k;
                        e.motion.y = x.position.y / k;
                    } else if (x.kind == PointerKind::down || x.kind == PointerKind::up) {
                        e.type = x.kind == PointerKind::down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                        e.button.windowID = id;
                        e.button.button = sdl_button_of(x.button == MouseButton::none ? MouseButton::left : x.button);
                        e.button.down = x.kind == PointerKind::down;
                        e.button.clicks = x.clicks;
                        e.button.x = x.position.x / k;
                        e.button.y = x.position.y / k;
                    } else {
                        push = false;
                    }
                } else if constexpr (std::same_as<T, WheelEvent>) {
                    e.type = SDL_EVENT_MOUSE_WHEEL;
                    e.wheel.windowID = id;
                    e.wheel.x = -x.delta.x / wheel_line;
                    e.wheel.y = -x.delta.y / wheel_line;
                    e.wheel.integer_x = static_cast<int>(e.wheel.x);
                    e.wheel.integer_y = static_cast<int>(e.wheel.y);
                    e.wheel.mouse_x = x.position.x / k;
                    e.wheel.mouse_y = x.position.y / k;
                    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
                } else if constexpr (std::same_as<T, KeyEvent>) {
                    e.type = x.kind == KeyKind::down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                    e.key.windowID = id;
                    e.key.key = keycode_of(x.key);
                    e.key.mod = keymod_of(x.mods);
                    e.key.down = x.kind == KeyKind::down;
                    e.key.repeat = x.repeat;
                } else if constexpr (std::same_as<T, TextEvent>) {
                    w.synthesized_text.push_back(x.text);
                    e.type = SDL_EVENT_TEXT_INPUT;
                    e.text.windowID = id;
                    e.text.text = w.synthesized_text.back().c_str();
                } else if constexpr (std::same_as<T, CloseRequestEvent>) {
                    e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
                    e.window.windowID = id;
                } else {
                    push = false;
                }
            },
            ev);
        if (push) SDL_PushEvent(&e);
    }
}

std::expected<raster::Surface, std::string> read_back(Window& w) {
    if (!w.renderer || !w.texture) return std::unexpected("nothing shown yet");
    // Draw the last frame again and read it before presenting (a presented
    // back buffer may be gone).
    SDL_RenderClear(w.renderer);
    SDL_RenderTexture(w.renderer, w.texture, nullptr, nullptr);
    SDL_Surface* s = SDL_RenderReadPixels(w.renderer, nullptr);
    if (!s) return std::unexpected(std::format("SDL_RenderReadPixels: {}", SDL_GetError()));
    SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!rgba) return std::unexpected(std::format("SDL_ConvertSurface: {}", SDL_GetError()));
    raster::Surface out(rgba->w, rgba->h);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(out.row(y), static_cast<const std::uint8_t*>(rgba->pixels) + static_cast<std::ptrdiff_t>(y) * rgba->pitch,
                    static_cast<std::size_t>(rgba->w) * 4);
    SDL_DestroySurface(rgba);
    return out;
}

}  // namespace omui::platform
