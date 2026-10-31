// openmui.accessibility:adapter — the platform's accessibility service, served
// from a Mirror: UI Automation for a window's HWND, NSAccessibility for an
// NSWindow, AT-SPI over D-Bus on Linux (no window needed). Attached once, it
// answers the system's first question (activation) from the mirror and is
// told, after each frame, what changed; what assistive technology asks waits
// in the mirror until the host serves it between frames.
export module openmui.accessibility:adapter;

import std;
import openmui.core;
import openmui.view;
import :map;
import :platform;

export namespace omui::accessibility {

// Whether this build serves a platform's assistive technology at all.
inline constexpr bool available = platform::available;
// The service served: "uia", "nsaccessibility", "atspi", or "none".
[[nodiscard]] std::string_view service() noexcept;

struct AdapterOptions {
    std::string toolkit = "openmui";
    std::string toolkit_version = "0.1.0";
    float       scale = 1;  // physical pixels per logical pixel
};

// The tree as a platform adapter would first be told it: AccessKit's own
// debug text of the full update ("" where there is no AccessKit).
[[nodiscard]] std::string debug(const Mirror& mirror, const AdapterOptions& options = {});

class Adapter {
public:
    // `window`: the HWND (Windows) or NSWindow* (macOS) the tree belongs to;
    // ignored on Linux. On Windows, before the window is first shown.
    [[nodiscard]] static std::expected<std::unique_ptr<Adapter>, std::string> attach(Mirror& mirror, void* window,
                                                                                    AdapterOptions options = {});
    ~Adapter();
    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;

    // After a frame: what changed in the tree, told to the platform when
    // assistive technology listens (and read again when it starts to).
    void update();
    // The window gained or lost the keyboard focus.
    void window_focus(bool focused);
    void set_scale(float scale);
    // X11: the window's place on the screen, with and without its frame
    // (logical pixels; Wayland has none to give).
    void window_bounds(Rect outer, Rect inner);
    // Whether assistive technology has asked for the tree.
    [[nodiscard]] bool active() const noexcept;
    struct Impl;

private:
    explicit Adapter(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::accessibility
