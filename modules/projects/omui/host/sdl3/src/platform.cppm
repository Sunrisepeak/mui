// openmui.host.sdl3:platform — the window, as the rest of the host sees it:
// no SDL type crosses this line (SDL lives in native/sdl.cpp).
module openmui.host.sdl3:platform;

import std;
import openmui.core;
import openmui.raster;
import openmui.view;

namespace omui::platform {

struct WindowOptions {
    std::string title;
    Size        size;
    bool        resizable = true;
    bool        vsync = true;
    std::string video_driver;
    std::string render_driver;
    bool        gpu = false;  // a GPU device claims the window: no SDL renderer
    // No system frame: the interface draws its own (D-75), over a window
    // that shows what is behind where it draws nothing (its shadow).
    bool        framework = false;
    // Made hidden, shown by `show` (UI Automation's adapter is attached to
    // a window before it is first shown).
    bool        hidden = false;
};

struct Window;  // SDL_Window, SDL_Renderer, the frame's texture

struct WindowDeleter {
    void operator()(Window* w) const noexcept;
};
using WindowPtr = std::unique_ptr<Window, WindowDeleter>;

std::expected<WindowPtr, std::string> open(const WindowOptions& options);

struct Polled {
    std::vector<InputEvent>    events;
    bool                       quit = false;
    std::optional<WindowState> state;  // the system changed it
};

// ── the window as a window (D-75) ──
[[nodiscard]] WindowState state(Window& w);
void show(Window& w);
// The system's own window: the HWND on Windows, the NSWindow on macOS
// (null elsewhere): what the accessibility adapter is attached to.
[[nodiscard]] void* native_window(Window& w);
void minimize(Window& w);
void toggle_maximize(Window& w);
void set_fullscreen(Window& w, bool on);
// With the system's frame, or without it (the interface's own).
void set_framed(Window& w, bool framed);
// Whether what the interface leaves transparent shows what is behind.
[[nodiscard]] bool transparent(Window& w);
// What lies under a point (logical pixels), for the system's moving and
// resizing of a frameless window; none: the system's own. False when the
// system cannot ask.
bool set_hit_test(Window& w, std::function<WindowArea(Point)> hit);
// Input goes to what lies beneath the window (or back to it); false when
// the system cannot (Wayland, offscreen).
bool pass_through(Window& w, bool on);
// The window's pending events, translated; with `wait`, sleeps up to
// `timeout` (none: until an event) for the first one.
Polled poll(Window& w, bool wait, std::optional<std::chrono::milliseconds> timeout);

[[nodiscard]] Size  logical_size(Window& w);
// What rhi::sdl::Options::window takes.
[[nodiscard]] std::uint32_t window_id(Window& w);
[[nodiscard]] float pixel_scale(Window& w);
// The refresh rate of the display the window is on (Hz; 0 when it says none).
[[nodiscard]] double refresh_hz(Window& w);
void present(Window& w, const raster::Surface& frame);
void text_input(Window& w, std::optional<Rect> caret);
void cursor(Window& w, std::string_view shape);
[[nodiscard]] std::string clipboard();
void set_clipboard(std::string_view text);
// The system's handler of an address (the browser, the mail program).
bool open_url(std::string_view url);
// Wakes a poll that sleeps (from any thread).
void wake();
// The person's languages as the system states them, best first, as BCP 47
// tags (SDL's preferred locales: the settings on Windows and macOS).
[[nodiscard]] std::vector<std::string> preferred_locales();
void synthesize(Window& w, std::span<const InputEvent> events);
std::expected<raster::Surface, std::string> read_back(Window& w);

}  // namespace omui::platform
