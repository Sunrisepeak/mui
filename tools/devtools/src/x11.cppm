// omui.devtools.x11 — what a window shows, seen from outside its process (lead design §2a.1: start,
// frames and interaction measured from outside). A small client of the X protocol over the
// display's local socket (mcpplibs.os's local connections), with the extensions it needs:
// DAMAGE, to be told when anything is drawn and when, XTEST, to press a button as a person does,
// and RANDR, to know the monitors and their rates. A display is the harness's own (an Xvfb it
// started: every drawing on the screen is the program's under test), or a desktop's (RM1: this
// machine's real display, its window manager and compositor, authorised by the session's
// MIT-MAGIC-COOKIE-1): there only the program's own window is watched and read — its damage, its
// pixels — never the screen or another's window.
//
// The pixels are read back (GetImage) only when something was drawn: a frame is "presented" when
// the server has it, the moment the application's drawing request (a GL or Vulkan present, a
// shared-memory image) was carried out — what a compositor would take.
export module omui.devtools.x11;

import std;

export namespace omui::devtools::x11 {

using Clock = std::chrono::steady_clock;

struct Rect {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
};

// An event as this client received it.
struct Event {
    enum class Kind { created, mapped, unmapped, configured, destroyed, damaged, other };
    Kind kind = Kind::other;
    std::uint32_t window = 0;  // the window it is about (the root, for damage: the screen)
    std::uint32_t parent = 0;  // created: the parent
    Rect area;                 // damaged: what was drawn; created, configured: the window
    Clock::time_point at;      // when it was read
};

// A monitor as RANDR says it: its output's name, its place on the screen, its mode's refresh rate.
struct Monitor {
    std::string name;
    Rect area;
    double hertz = 0;
    bool primary = false;
};

// An Xauthority file's MIT-MAGIC-COOKIE-1 for the display `:number` on this machine (a local or
// wildcard entry whose number is the display's or none), as the X library would pick it; none
// when the file has no such entry.
[[nodiscard]] std::optional<std::string> cookie(std::string_view xauthority, int number,
                                                std::string_view hostname);

class Display {
public:
    // The display `:number`, through its local socket; authorised with the session's cookie
    // ($XAUTHORITY, else ~/.Xauthority) where it has one for the display.
    [[nodiscard]] static std::expected<Display, std::string> open(int number);

    Display(Display&&) noexcept;
    Display& operator=(Display&&) noexcept;
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;
    ~Display();

    [[nodiscard]] std::uint32_t root() const noexcept;
    [[nodiscard]] Rect screen() const noexcept;

    // Structure events of the root's children (created, mapped, configured, gone); with `screen`,
    // a damage report for every drawing on the screen, each a rectangle (the harness's own server:
    // nothing else draws there). On a desktop, follow() and damage() the program's own window.
    [[nodiscard]] std::expected<void, std::string> watch(bool screen = true);
    // A window's own structure events (mapped, configured, gone), sent for it whoever its parent
    // (a window manager's frame).
    [[nodiscard]] std::expected<void, std::string> follow(std::uint32_t window);
    // A damage report for every drawing into `window` (its inferiors included), in its own
    // coordinates; the event's `window` is it.
    [[nodiscard]] std::expected<void, std::string> damage(std::uint32_t window);
    // The next event, waiting at most `wait` (nothing when none came).
    [[nodiscard]] std::expected<std::optional<Event>, std::string>
    next(std::chrono::milliseconds wait);

    // The pixels of `area` of `drawable` (the root, the screen, when none is named; a window's own,
    // composited or not, when one is), 0x00RRGGBB row by row, and when the server answered.
    struct Image {
        std::vector<std::uint32_t> pixels;
        Clock::time_point at;
    };
    [[nodiscard]] std::expected<Image, std::string> image(Rect area, std::uint32_t drawable = 0);
    // A window's place relative to its parent and its size.
    [[nodiscard]] std::expected<Rect, std::string> geometry(std::uint32_t window);
    // A window's top-left corner on the screen (TranslateCoordinates to the root).
    [[nodiscard]] std::expected<std::array<std::int32_t, 2>, std::string>
    origin(std::uint32_t window);
    // The root's child that holds `window` (itself, or a window manager's frame around it).
    [[nodiscard]] std::expected<std::uint32_t, std::string> top_level(std::uint32_t window);
    // A window's children, bottom to top.
    [[nodiscard]] std::expected<std::vector<std::uint32_t>, std::string>
    children(std::uint32_t window);
    // The biggest window (at least 64 × 64) a process says is its (_NET_WM_PID), at the top or in
    // a window manager's frame there; 0 when it has none.
    [[nodiscard]] std::expected<std::uint32_t, std::string> window_of(std::uint64_t process);
    // Where the pointer is on the screen, and the root's child under it (0: none).
    struct Pointer {
        std::int32_t x = 0, y = 0;
        std::uint32_t over = 0;
    };
    [[nodiscard]] std::expected<Pointer, std::string> where_pointer();
    // The X resource database the desktop set on the root (RESOURCE_MANAGER: "Xft.dpi:\t120\n…");
    // empty where none is set.
    [[nodiscard]] std::expected<std::string, std::string> resources();
    // The monitors (RANDR 1.5's), each with its refresh rate; none on a server without RANDR.
    [[nodiscard]] std::expected<std::vector<Monitor>, std::string> monitors();
    // A window made the active one by its window manager (_NET_ACTIVE_WINDOW, asked as a pager
    // asks): it takes the keyboard's focus.
    [[nodiscard]] std::expected<void, std::string> activate(std::uint32_t window);
    // The window with the keyboard's focus (GetInputFocus).
    [[nodiscard]] std::expected<std::uint32_t, std::string> input_focus();
    // The key that types `keysym` on the keyboard's first level (GetKeyboardMapping), 0 for none.
    [[nodiscard]] std::expected<std::uint8_t, std::string> keycode(std::uint32_t keysym);
    // A key pressed or let go where the keyboard's focus is (XTEST).
    [[nodiscard]] std::expected<void, std::string> key(std::uint8_t keycode, bool down);
    // A window kept above the others by its window manager (_NET_WM_STATE_ABOVE, asked as a pager
    // asks): where a desktop's other windows overlap it, the pointer reaches it. Nothing where no
    // window manager answers.
    [[nodiscard]] std::expected<void, std::string> keep_above(std::uint32_t window);
    // A window moved to a place on the screen: its frame's corner there, asked of its window
    // manager as a pager asks (_NET_MOVERESIZE_WINDOW; nothing moves where none answers).
    [[nodiscard]] std::expected<void, std::string> move(std::uint32_t window, std::int32_t x,
                                                        std::int32_t y);
    // The process a window says it belongs to (_NET_WM_PID), 0 where it says none.
    [[nodiscard]] std::expected<std::uint64_t, std::string> process_of(std::uint32_t window);
    // A window's size set (no window manager: the server does it).
    [[nodiscard]] std::expected<void, std::string> resize(std::uint32_t window, std::int32_t width,
                                                          std::int32_t height);
    // The pointer moved to a place on the screen; the first button pressed or let go there (XTEST).
    [[nodiscard]] std::expected<void, std::string> pointer(std::int32_t x, std::int32_t y);
    [[nodiscard]] std::expected<void, std::string> button(bool down);
    // One notch of the wheel where the pointer is (buttons 4 and 5: up, down), pressed and let go;
    // not waited for.
    [[nodiscard]] std::expected<void, std::string> wheel(bool down);
    // A round trip: everything sent before has been carried out.
    [[nodiscard]] std::expected<void, std::string> sync();
private:
    Display();
    struct State;
    std::unique_ptr<State> state_;
};

// A picture's 64-bit FNV-1a (mlibs.hash) over its pixels' colour (the unused byte left out).
[[nodiscard]] std::uint64_t hash(std::span<const std::uint32_t> pixels) noexcept;
// Whether every pixel has the same colour (a window cleared, nothing drawn on it yet).
[[nodiscard]] bool uniform(std::span<const std::uint32_t> pixels) noexcept;

}  // namespace omui::devtools::x11
