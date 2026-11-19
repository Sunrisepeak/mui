// omui.devtools.x11 — what a window shows, seen from outside its process (lead design §2a.1: start,
// frames and interaction measured from outside). A small client of the X protocol over the
// display's local socket (mcpplibs.os's local connections), with the two extensions it needs:
// DAMAGE, to be told when anything is drawn on the screen and when, and XTEST, to press a button as
// a person does. A display is the harness's own (an Xvfb it started), so every drawing on the
// screen is the program's under test.
//
// The screen's pixels are read back (GetImage) only when something was drawn: a frame is
// "presented" when the server has it, the moment the application's drawing request (a GL or Vulkan
// present, a shared-memory image) was carried out — what a compositor would take.
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

class Display {
public:
    // The display `:number`, through its local socket.
    [[nodiscard]] static std::expected<Display, std::string> open(int number);

    Display(Display&&) noexcept;
    Display& operator=(Display&&) noexcept;
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;
    ~Display();

    [[nodiscard]] std::uint32_t root() const noexcept;
    [[nodiscard]] Rect screen() const noexcept;

    // Structure events of the root's children (created, mapped, configured, gone) and a damage
    // report for every drawing on the screen, each a rectangle.
    [[nodiscard]] std::expected<void, std::string> watch();
    // The next event, waiting at most `wait` (nothing when none came).
    [[nodiscard]] std::expected<std::optional<Event>, std::string>
    next(std::chrono::milliseconds wait);

    // The screen's pixels in `area` (0x00RRGGBB, row by row), and when the server answered.
    struct Image {
        std::vector<std::uint32_t> pixels;
        Clock::time_point at;
    };
    [[nodiscard]] std::expected<Image, std::string> image(Rect area);
    // A window's place relative to its parent and its size.
    [[nodiscard]] std::expected<Rect, std::string> geometry(std::uint32_t window);
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
