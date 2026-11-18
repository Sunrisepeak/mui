// omui.accessibility:adapter — the platform's accessibility service, served
// from a Mirror: UI Automation for a window's HWND, NSAccessibility for an
// NSWindow, AT-SPI over D-Bus on Linux (no window needed). Attached once, it
// answers the system's first question (activation) from the mirror and is
// told, after each frame, what changed; what assistive technology asks waits
// in the mirror until the host serves it between frames.
//
// The service is a bridge's (B6): omui.accessibility.bridge installs
// AccessKit's where it has a library; an application that leaves the bridge
// out keeps the mapping alone, and `available()` is false.
export module omui.accessibility:adapter;

import std;
import mlibs.compose;
import omui.core;
import omui.view;
import :map;

export namespace omui::accessibility {

struct AdapterOptions {
    std::string toolkit = "openmui";
    std::string toolkit_version = "0.1.0";
    float       scale = 1;  // physical pixels per logical pixel
};

// One attachment of a platform's service to a mirror, as a bridge serves it.
class Session {
public:
    virtual ~Session() = default;
    virtual void update() = 0;
    virtual void window_focus(bool focused) = 0;
    virtual void set_scale(float scale) = 0;
    virtual void window_bounds(Rect outer, Rect inner) = 0;
    [[nodiscard]] virtual bool active() const noexcept = 0;
};

// A platform's service: its name ("uia", "nsaccessibility", "atspi") and how
// it attaches to a mirror and window. Only what a host calls: whatever the
// table names stays in a program that installs it.
struct Service {
    std::string_view name;
    std::expected<std::unique_ptr<Session>, Failure> (*attach)(Mirror& mirror, void* window, const AdapterOptions& options) = nullptr;
};

// The slot a platform's service is installed at (mlibs.compose): one provider. A program without one —
// built without the bridge, or on a platform AccessKit has no library for — serves no assistive technology.
inline constexpr mlibs::compose::Slot<Service> service_slot{
    {.id = "omui.accessibility.service",
     .summary = "the platform's assistive-technology service (UI Automation, NSAccessibility, AT-SPI)",
     .absent = "build with omui.sdk's feature `accessibility-bridge`, or depend on omui.accessibility.bridge and call "
               "omui::accessibility::bridge::install() before a host is made"}};

// The bridge's: once, before a host attaches (omui.accessibility.bridge::install), into the process's
// composition under the service's name.
void install(const Service& service) noexcept;
// Whether a platform's service is installed.
[[nodiscard]] bool available() noexcept;
// The service served: "uia", "nsaccessibility", "atspi", or "none".
[[nodiscard]] std::string_view service() noexcept;

class Adapter {
public:
    // `window`: the HWND (Windows) or NSWindow* (macOS) the tree belongs to;
    // ignored on Linux. On Windows, before the window is first shown. Refused
    // where no service is installed.
    [[nodiscard]] static std::expected<std::unique_ptr<Adapter>, Failure> attach(Mirror& mirror, void* window,
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

private:
    explicit Adapter(std::unique_ptr<Session> session);
    std::unique_ptr<Session> session_;
};

}  // namespace omui::accessibility
