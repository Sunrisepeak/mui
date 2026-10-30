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
// The page hears more than the canvas (src/js/omui_web.js): a hidden
// textarea at the caret, where input methods compose (their composition is
// openmui's preedit, their result its text); the clipboard, as the page last
// knew it, written through the Async Clipboard API; the semantic tree as a
// hidden DOM after each batch of changes (DomMirror: ARIA roles, names,
// values, states, focus, a live region), which a screen reader reads and
// moves the focus in; window.omui, through which the page's agents use
// openmui.tree@0.1 as the desktop's agents do — over the bus, so openas
// decides (S-11), and never as the person: the person is present only in
// the page's own input (isTrusted events; openas §2.7). The program's store
// is the page's localStorage.
//
// Off the Web the host has no page: no input arrives and nothing is shown,
// but step(), synthesize() and request() run the same loop (its tests).
export module openmui.host.web;

import std;
export import openmui.core;
export import openmui.view;
export import openmui.semantic;
export import openmui.raster;
export import openmui.native.store;
export import :mirror;

export namespace omui {

// Who draws the frames (D-84 §6.3), as the desktop's host decides it: the
// page's WebGPU, its WebGL2, or the CPU. `automatic` takes the first GPU the
// page has — a software adapter (SwiftShader, llvmpipe) counting as none:
// the CPU draws faster than it; `gpu` insists on one (either, software too);
// `webgpu` and `webgl` on that one. Without what they ask for, the CPU draws.
// The CPU's pixels are the reference, the same bytes everywhere; a GPU's are
// within the renderer's stated tolerances of them.
enum class Renderer : std::uint8_t { automatic, cpu, gpu, webgpu, webgl };

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
    Renderer                renderer = Renderer::automatic;
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

    // What a person did in the page's hidden DOM, through assistive
    // technology: moved the focus to a node, or activated it (as a press of
    // its middle). Taken at the next step, as the page's are.
    struct PageAction {
        enum class Kind : std::uint8_t { focus, activate };
        Kind        kind = Kind::activate;
        std::string node;
    };
    void synthesize(std::span<const PageAction> actions);

    // A request as window.omui makes it (openmui.tree@0.1, one JSON line),
    // served at the next step, between frames, with its records to `reply`.
    // It comes over the bus: openas decides it (S-11), and a person's own
    // action is refused to it whatever actor it names (S-10).
    void request(std::string_view line, std::function<void(std::string_view record)> reply);

    // The program's store (omui::store() while this host lives): the page's
    // localStorage, under the application's id; memory without a page.
    [[nodiscard]] Store& store() noexcept;
    // Called with true when the page is hidden (another tab, the app in the
    // background) and false when it shows again — before the runtime hears
    // it as the window's focus. Toy's lifecycle hangs here.
    void on_visibility(std::function<void(bool hidden)> fn);

    // What the canvas shows, when the CPU draws it.
    [[nodiscard]] const raster::Surface& pixels() const noexcept;
    // Who draws: "webgpu", "webgl" or "cpu".
    [[nodiscard]] std::string_view renderer() const noexcept;
    // The last frame shown, read back from whoever drew it — a GPU's texture
    // (a program linked to wait for it: rhi::webgpu::can_wait), the CPU's
    // frame: what a test compares with the reference, a screenshot keeps.
    [[nodiscard]] std::expected<raster::Surface, std::string> read_frame();
    [[nodiscard]] std::size_t frames_shown() const noexcept;
    [[nodiscard]] bool hidden() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// Whether this program runs in a page (wasm32-emscripten, with a document: not node).
[[nodiscard]] bool in_browser() noexcept;

// The person's languages as the browser states them (navigator.languages).
[[nodiscard]] std::vector<std::string> browser_locales();

}  // namespace omui
