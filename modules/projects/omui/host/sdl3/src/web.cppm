// openmui.host.sdl3:web — web pages in the system's own web view, placed in
// the window over a node (D-141: an external surface). Declared on every
// system, defined where the system has one (macOS: WKWebView, through
// mcpplibs.os's Objective-C runtime; src/macos, built for it alone) and
// called only in a branch `if constexpr` keeps for that system: elsewhere the
// host has no web view, and the component draws its card.
module omui.host.sdl3:web;

import std;
import omui.core;
import omui.view;

namespace omui::web {

// The pages of one window.
struct Pages;

// The pages of the window (the system's own: an NSWindow), or none.
[[nodiscard]] std::unique_ptr<Pages, void (*)(Pages*)> open_pages(void* system_window);
// A page at `url`, told each change of it (from poll()); 0: it could not be made.
[[nodiscard]] std::uint64_t open(Pages& pages, std::string_view url, std::function<void(const WebPage&)> changed);
// Where it is in the window (logical pixels, from the top left; the window `height` tall) and whether it shows.
void place(Pages& pages, std::uint64_t page, Rect bounds, bool shown, float height);
// "navigate" (an address), "back", "forward", "reload".
[[nodiscard]] bool command(Pages& pages, std::uint64_t page, std::string_view what, std::string_view argument);
void close(Pages& pages, std::uint64_t page);
// Each page as it is now, told where it changed (each turn of the loop).
void poll(Pages& pages);

}  // namespace omui::web
