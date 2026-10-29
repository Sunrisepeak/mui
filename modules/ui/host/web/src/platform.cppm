// openmui.host.web:platform — the page, as the rest of the host sees it: no
// Emscripten type crosses this line (the page is native/browser/html5.cpp;
// off the Web, native/elsewhere/none.cpp).
module openmui.host.web:platform;

import std;
import openmui.core;
import openmui.raster;
import mcpplibs.os;

namespace omui::page {

inline constexpr bool here = mcpplibs::os::is_emscripten;

// Listens to the page's input on the canvas (keys on the window); false
// where there is no page.
bool open(std::string_view canvas, std::string_view title);

struct Canvas {
    Size  size;           // CSS pixels: logical
    float scale = 1.0f;   // the device's pixel ratio
};
[[nodiscard]] Canvas canvas(std::string_view selector);

// What came since the last call: the page's events in order, then the
// gamepads' changes since their last sample (spec §6.1).
void take(std::vector<InputEvent>& out);

void present(std::string_view selector, const raster::Surface& frame);
[[nodiscard]] bool hidden();

// The page calls `tick(user)` once a display frame, from now on; on the Web
// it does not return. False where there is no page.
bool loop(void (*tick)(void*), void* user);

[[nodiscard]] std::vector<std::string> locales();

}  // namespace omui::page
