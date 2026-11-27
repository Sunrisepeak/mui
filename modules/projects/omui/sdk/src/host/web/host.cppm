// openmui:host — the host `omui::run` shows the application with: on the Web, the page's canvas.
export module omui:host;

export import omui.host.web;

// What `omui::run` carries here beside showing the application (src/app.cpp). A page is its own host and its own
// window: no headless run (--headless, --serve, --static-html, --agent-guide, --png, --replay, --present-to are a
// program's on its own, where they write files and serve an endpoint), and no frame of the framework's around it.
// The program a page loads carries neither (measured on oweb.reader: 2026-11-27-web-program-size-design.md).
namespace omui::hosting {
inline constexpr bool runs_headless = false;
inline constexpr bool frames_window = false;
}  // namespace omui::hosting
