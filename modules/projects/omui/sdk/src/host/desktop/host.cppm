// openmui:host — the host `omui::run` shows a window with: on the desktop, SDL3's; and the terminal's (--terminal).
export module omui:host;

export import omui.host.sdl3;
export import omui.host.terminal;

// What `omui::run` carries here beside showing the application (src/app.cpp): headless runs (--headless, --serve,
// --static-html, --agent-guide, --png, --replay, --present-to) and the framework's frame around a window (D-75).
namespace omui::hosting {
inline constexpr bool runs_headless = true;
inline constexpr bool frames_window = true;
}  // namespace omui::hosting
