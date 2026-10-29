// openmui.host.web — openmui in the browser (D-84 §6.2).
//
//     omui::Web w({.title = "Notes", .app = "org.example.notes"});
//     w.mount("main", notes());
//     return w.run();   // the page's frame loop from now on
//
// A Web owns a runtime on the interactive clock and a canvas. The page calls
// it once a display frame (requestAnimationFrame): it takes the input that
// came since — pointer, touch, wheel, keys, the gamepads sampled — and runs a
// frame when something wants one (D-70: none while nothing moves, at the rate
// a lease asks while something does), draws it with the CPU renderer (only
// the rows that changed) and puts it on the canvas. One thread: the page's.
//
// Off the Web the host has no page: no input arrives and nothing is shown,
// but step() and synthesize() run the same loop (its tests).
export module openmui.host.web;

import std;
export import openmui.core;
export import openmui.view;
export import openmui.semantic;
export import openmui.raster;

export namespace omui {

struct WebOptions {
    std::string             title = "openmui";   // the document's
    std::string             app = "openmui-app";
    std::string             canvas = "#canvas";  // the canvas's selector
    Size                    size{960, 640};      // until the canvas says its own (off the Web: always)
    Theme                   theme = Theme::light();
    typeset::FontCollection* fonts = nullptr;     // nullptr: the default fonts (the page's /fonts)
    bool                    reduce_motion = false;
    bool                    reduce_transparency = false;
    Color                   background = Color::white();
    // Show a runtime that exists already (the host does not own it).
    Runtime*                runtime = nullptr;
};

class Web {
public:
    explicit Web(WebOptions options = {});
    ~Web();
    Web(const Web&) = delete;
    Web& operator=(const Web&) = delete;

    [[nodiscard]] Runtime& runtime() noexcept;
    [[nodiscard]] semantic::Tree& tree() noexcept;
    NodeId mount(std::string_view slot, View view);

    // Hands the loop to the page: on the Web it does not come back (the page
    // calls step() once a display frame from now on); off the Web it steps
    // until quit(). Answers the exit code.
    int run();
    // One turn: the input since the last, a frame, shown when it changed
    // (the page's own turns frame only when one is wanted). Whether a frame ran.
    bool step();
    // The page's next turn runs a frame (a change made outside a frame: a
    // promise of the page's that settled, say).
    void wake();
    void quit(int code = 0);

    // Events as the page would give them, taken at the next step (tests, automation).
    void synthesize(std::span<const InputEvent> events);
    // Called with true when the page is hidden (another tab, the app in the
    // background) and false when it shows again — before the runtime hears
    // it as the window's focus. Toy's lifecycle hangs here.
    void on_visibility(std::function<void(bool hidden)> fn);

    // What the canvas shows (the CPU frame).
    [[nodiscard]] const raster::Surface& pixels() const noexcept;
    [[nodiscard]] std::size_t frames_shown() const noexcept;
    [[nodiscard]] bool hidden() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// Whether this program runs in a page (wasm32-emscripten).
[[nodiscard]] bool in_browser() noexcept;

// The person's languages as the browser states them (navigator.languages).
[[nodiscard]] std::vector<std::string> browser_locales();

}  // namespace omui
