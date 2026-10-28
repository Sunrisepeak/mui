// openmui.host.sdl3:platform — the window, as the rest of the host sees it:
// no SDL type crosses this line (SDL lives in native/sdl.cpp).
module openmui.host.sdl3:platform;

import std;
import openmui.core;
import openmui.raster;

namespace omui::platform {

struct WindowOptions {
    std::string title;
    Size        size;
    bool        resizable = true;
    bool        vsync = true;
    std::string video_driver;
    std::string render_driver;
    bool        gpu = false;  // a GPU device claims the window: no SDL renderer
};

struct Window;  // SDL_Window, SDL_Renderer, the frame's texture

struct WindowDeleter {
    void operator()(Window* w) const noexcept;
};
using WindowPtr = std::unique_ptr<Window, WindowDeleter>;

std::expected<WindowPtr, std::string> open(const WindowOptions& options);

struct Polled {
    std::vector<InputEvent> events;
    bool                    quit = false;
};
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
// Wakes a poll that sleeps (from any thread).
void wake();
// The person's languages as the system states them, best first, as BCP 47
// tags (SDL's preferred locales: the settings on Windows and macOS).
[[nodiscard]] std::vector<std::string> preferred_locales();
void synthesize(Window& w, std::span<const InputEvent> events);
std::expected<raster::Surface, std::string> read_back(Window& w);

}  // namespace omui::platform
