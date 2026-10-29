// openmui.host.web:platform — the page, as the rest of the host sees it: no
// Emscripten type crosses this line (the page is native/browser/html5.cpp;
// off the Web, native/elsewhere/none.cpp).
module openmui.host.web:platform;

import std;
import openmui.core;
import openmui.raster;

namespace omui::page {

// Whether the program runs in a page (not node, not a native target).
[[nodiscard]] bool exists();

// Listens to the page's input on the canvas (keys on the window); false
// where there is no page.
bool open(std::string_view canvas, std::string_view title);

struct Canvas {
    Size  size;           // CSS pixels: logical
    float scale = 1.0f;   // the device's pixel ratio
};
[[nodiscard]] Canvas canvas(std::string_view selector);

// What a person did through the page's hidden DOM (assistive technology
// moved the focus, or activated an element), or a request of window.omui's.
struct Request {
    enum class Kind : std::uint8_t { agent, focus, activate };
    Kind        kind = Kind::agent;
    std::string text;  // agent: the request's line; else the node's id
};

// What came since the last call: the page's events in order (the canvas's,
// the keys', the hidden textarea's text and composition), then the gamepads'
// changes since their last sample (spec §6.1); and the requests.
void take(std::vector<InputEvent>& out, std::vector<Request>& requests);

// The hidden textarea input methods compose in: at the caret (logical
// pixels on the canvas), named for the field; none: put away.
void text_input(std::optional<Rect> caret, std::string_view label);
// A batch of the semantic DOM mirror's (DomMirror), as JSON.
void mirror(std::string_view batch);
// A record line answering a window.omui request.
void agent_reply(std::string_view line);

// The clipboard as the page last knew it (what was pasted or copied);
// writing goes to the Async Clipboard API as well.
[[nodiscard]] std::string clipboard();
void set_clipboard(std::string_view text);

// localStorage, where the page has it (keys as given: the caller prefixes them).
[[nodiscard]] bool storage();
[[nodiscard]] std::optional<std::string> storage_get(std::string_view key);
[[nodiscard]] bool storage_set(std::string_view key, std::string_view value);  // false: full
bool storage_remove(std::string_view key);
[[nodiscard]] std::vector<std::string> storage_keys(std::string_view prefix);

void present(std::string_view selector, const raster::Surface& frame);
[[nodiscard]] bool hidden();

// The page calls `tick(user)` once a display frame, from now on; on the Web
// it does not return. False where there is no page.
bool loop(void (*tick)(void*), void* user);

[[nodiscard]] std::vector<std::string> locales();

}  // namespace omui::page
