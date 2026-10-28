// openmui.view:window — the window an interface is shown in (D-75), as the
// interface sees it: its state (normal, maximized, minimized, fullscreen),
// whether it is active, whether it is closing, and whether its frame is the
// interface's own (framework-drawn) or the system's. What can be done to it —
// minimize, maximize, fullscreen, close — is a verb each (`window.*`), so a
// title bar's button, a shortcut and an agent all do it the same way; a host
// implements them (WindowHost). For a framework-drawn frame the host asks
// what lies under a point (window_area_at): the edges resize, a node marked
// `window_area: "drag"` moves the window, anything that can be operated is
// the interface's.
export module openmui.view:window;

import std;
import openmui.core;
import :runtime;

export namespace omui {

enum class WindowState : std::uint8_t { normal, maximized, minimized, fullscreen };
[[nodiscard]] std::string_view name(WindowState s) noexcept;

// What a host does for the interface's window.
class WindowHost {
public:
    virtual ~WindowHost() = default;
    [[nodiscard]] virtual bool framework_drawn() const noexcept = 0;
    virtual void minimize() = 0;
    virtual void toggle_maximize() = 0;
    virtual void set_fullscreen(bool on) = 0;
    // As the system's close button: the application decides (it may keep
    // the window open to save), then the window closes.
    virtual void request_close() = 0;
    // Input goes to what lies beneath the window (while it dissolves on
    // closing); false when the system cannot.
    virtual bool pass_through(bool on) = 0;
};

// The window of this thread's interface (one a UI thread).
class Window {
public:
    Window();

    // A host shows the interface in a window: its verbs are added to
    // `verbs` (once) and do what the host does; state follows the host.
    void attach(WindowHost* host, Verbs& verbs);
    void detach() noexcept { host_ = nullptr; }
    [[nodiscard]] WindowHost* host() const noexcept { return host_; }

    // Set by the host as the system reports it.
    void set_state(WindowState s);
    void set_active(bool active);
    void set_closing(bool closing);

    // Read by views (signals: what reads them follows the window).
    [[nodiscard]] WindowState state() const;
    [[nodiscard]] bool active() const;
    [[nodiscard]] bool closing() const;
    [[nodiscard]] bool framework_drawn() const;
    Signal<std::string> state_name;  // "normal", "maximized", "minimized", "fullscreen"
    Signal<bool>        is_active;
    Signal<bool>        is_closing;
    Signal<bool>        is_framework;

private:
    WindowHost* host_ = nullptr;
};

[[nodiscard]] Window& window();

// What lies under `p` (logical pixels) of a window `size` big, from the
// deepest node out: what can be operated is the interface's (a button in the
// title bar); a frame component says what its parts are (window_area); a
// node marked `window_area: "drag"` moves the window. Failing all of them, an
// edge `margin` wide resizes a normal window.
[[nodiscard]] WindowArea window_area_at(const Runtime& rt, Point p, Size size, float margin = 6);

}  // namespace omui
