// omui.host.terminal — openmui in a terminal's character cells (terminal design, D-249).
//
// The same tree, layout and display list as a window's, read into cells (mrender.cells) instead of pixels: the
// components are the window's, unchanged. Text is measured in cells (mlibs.text's cell measure: a grapheme its
// cells, a line a row), the grid's cell is a size in the interface's logical pixels, the terminal's keys, text,
// mouse (SGR), paste and focus arrive as the events every host delivers (`terminal::Input`). A frame is a value —
// compared as text, drawn as what changed — so it runs headless as well as on the process's terminal.
//
//     omui::Terminal t({.columns = 40, .rows = 12});
//     t.mount("main", view());
//     t.step("\t\r");                  // Tab, then Enter: as a person types them
//     std::print("{}", t.frame().text());
//     // or, on this program's terminal until Ctrl+C:  return t.run();
//
// An agent reaches it as it does a window: the interface at the endpoint (`ui.*`, when the program installed the
// agent surface), and the terminal's picture as a subject beside it (`terminal.frame`, `terminal.at`).
export module omui.host.terminal;

import std;
export import omui.core;
export import omui.view;
export import omui.semantic;
export import mrender.cells;
export import :input;
import mlibs.agent;
import mlibs.compose;
import mlibs.control;
import omui.agent;

export namespace omui {

struct TerminalOptions {
    // A cell in the interface's logical pixels and the baseline in it: text is measured, and the list read, by it.
    mrender::cells::Grid    grid;
    // The size in cells where there is no terminal to ask (`step`); `run` takes the terminal's, as it changes.
    int                     columns = 80;
    int                     rows = 24;
    Theme                   theme = Theme::light();
    // The colours the terminal shows: `run` takes 24-bit where COLORTERM says so, else the 256.
    mrender::cells::Colors  colors = mrender::cells::Colors::truecolor;
    std::string             app = "openmui-app";  // the application id: the endpoint's name
    // Its endpoint, where the program installed the agent surface (omui::agent::install; the SDK's `agent`).
    bool                    agent = true;
    std::filesystem::path   endpoint_directory;   // default: agent::default_directory()
    std::string             endpoint_name;        // default: the application id
    bool                    agent_input = true;   // agents may give input as a person's devices would
    seconds                 frame_period = seconds(1.0 / 60);  // between frames while something moves
};

class Terminal {
public:
    explicit Terminal(TerminalOptions options = {});
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    [[nodiscard]] Runtime& runtime() noexcept;
    [[nodiscard]] semantic::Tree& tree() noexcept;
    [[nodiscard]] const TerminalOptions& options() const noexcept;
    NodeId mount(std::string_view slot, View view);

    // ── frames as values ──
    // One frame: the bytes a terminal would send (keys, text, the mouse, paste), decoded as `run` does, then
    // the frame drawn into cells, at `at` (default: a frame period after the last).
    const mrender::cells::Frame& step(std::string_view input = {}, std::optional<seconds> at = std::nullopt);
    // Frames until nothing moves and no timer is due within `limit`; how many ran.
    std::size_t settle(seconds limit = seconds(10));
    [[nodiscard]] const mrender::cells::Frame& frame() const noexcept;
    // The terminal's size changed: the interface is laid out again in that many cells.
    void resize(int columns, int rows);
    // The cell's centre in the interface's logical pixels: where a pointer in it is.
    [[nodiscard]] Point point_of(int column, int row) const noexcept;
    // The cell a node's drawn bounds centre on, nothing for a node not shown: where a person would click it.
    [[nodiscard]] std::optional<std::pair<int, int>> cell_of(std::string_view id) const;

    // ── this program's terminal ──
    // Shows the interface on the process's terminal until a person ends it (Ctrl+C) or `quit` is called: keys
    // read as typed, the alternate screen, the mouse and pasted text reported, each frame what changed in it; the
    // terminal as it was after. Input or output that is not a terminal (a pipe) is read and written the same way,
    // without the modes only a terminal has. The exit code.
    int run();
    void quit() noexcept;

    [[nodiscard]] agent::Endpoint* endpoint() noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

namespace terminal {

// How `omui::run` shows an application in this program's terminal (`--terminal`): a slot this package owns and
// provides at (mlibs.compose, D-238; the agent surface's pattern), so a program whose root does not install it
// carries no terminal. omui.sdk installs it by its feature `terminal`.
struct ShowService {
    // Shows what `prepare` mounts on a terminal made with `options`, until a person ends it; the exit code.
    int (*run)(TerminalOptions options, const std::function<std::shared_ptr<void>(Runtime&)>& prepare) = nullptr;
};
inline constexpr mlibs::compose::Slot<ShowService> show_slot{
    {.id = "omui.host.terminal.show",
     .summary = "an application shown in this program's terminal, in character cells (omui::run's --terminal)",
     .absent = "build with omui.sdk's feature `terminal` (omui.sdk = { …, features = [\"terminal\"] }), or call omui::terminal::install() before omui::run"}};

// Installs the terminal at `show_slot` in the process's composition: a program's root calls it.
void install();

// The terminal's picture as a subject of the agent kernel (D-173; its catalog: terminal.ops.xdef.ts): the frame as
// text and cells (`terminal.frame`), a cell back to the node that drew it (`terminal.at`). The host serves one at
// its endpoint; a test or a tool may make its own over a terminal.
class Subject final : public mlibs::agent::Subject {
public:
    explicit Subject(Terminal& host) noexcept : host_(host) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "terminal"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "omui.host.terminal@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true, .provenance = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override;
    [[nodiscard]] std::string_view listing() const noexcept override;
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override;
    // The cells' hash: the same picture, the same hash.
    [[nodiscard]] std::uint64_t state_hash() const override;
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    Terminal& host_;
};

}  // namespace terminal

}  // namespace omui
