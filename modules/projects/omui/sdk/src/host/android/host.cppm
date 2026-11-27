// openmui:host — the host `omui::run` shows the application with: on Android, the platform's NativeActivity window.
export module omui:host;

export import omui.host.android;

// What `omui::run` carries here beside showing the application (src/app.cpp): headless runs and the framework's
// frame around a window, as on the desktop.
namespace omui::hosting {
inline constexpr bool runs_headless = true;
inline constexpr bool frames_window = true;
}  // namespace omui::hosting
