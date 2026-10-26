// openmui.host.sdl3 — openmui on the desktop (plan §8.2).
//
//     omui::Desktop d({.title = "Notes", .app = "org.example.notes"});
//     d.mount("main", notes());
//     return d.run();
//
// A Desktop owns a runtime on the interactive clock and a window. Each turn
// of its loop gathers the window's events, serves agent requests, runs a
// frame, and shows it when it changed; between turns it sleeps until an
// event, the next timer, or an agent's request, and paces animation by the
// display's refresh.
export module openmui.host.sdl3;

import std;
export import openmui.core;
export import openmui.view;
export import openmui.semantic;
export import openmui.agent;
export import openmui.raster;

export namespace omui {

struct DesktopOptions {
    std::string             title = "openmui";
    Size                    size{960, 640};   // logical pixels
    bool                    resizable = true;
    std::string             app = "openmui-app";  // the application id (the agent endpoint's name)
    bool                    agent = true;         // serve openmui.tree@0.1
    std::filesystem::path   agent_directory;      // default: agent::default_directory()
    Theme                   theme = Theme::light();
    typeset::FontCollection* fonts = nullptr;
    bool                    reduce_motion = false;
    Color                   background = Color::white();
    bool                    vsync = true;
    std::string             video_driver;     // "" the platform's; "offscreen" in CI
    std::string             render_driver;    // "" the best; "software" for exact read-back
    bool                    parallel_raster = true;  // the CPU renderer draws bands of rows at the same time
};

class Desktop {
public:
    explicit Desktop(DesktopOptions options = {});
    ~Desktop();
    Desktop(const Desktop&) = delete;
    Desktop& operator=(const Desktop&) = delete;

    // Opens the window (run() opens it when it is not open yet).
    std::expected<void, std::string> open();
    [[nodiscard]] bool is_open() const noexcept;

    [[nodiscard]] Runtime& runtime() noexcept;
    [[nodiscard]] semantic::Tree& tree() noexcept;
    NodeId mount(std::string_view slot, View view);

    // Runs until the window closes or quit(); answers the exit code.
    int run();
    // One turn of the loop; `wait` sleeps until something happens. False once quitting.
    bool step(bool wait = false);
    void quit(int code = 0);
    // Wakes a loop sleeping in step(true) — from any thread (after posting to
    // the runtime's queue, say).
    void wake();

    // Events as the platform would deliver them: they go through the
    // window's event queue and the same translation as a person's input
    // (automation, tests).
    void synthesize(std::span<const InputEvent> events);
    // What the window shows, read back (software rendering reads exactly).
    [[nodiscard]] std::expected<raster::Surface, std::string> window_pixels();
    [[nodiscard]] float scale() const noexcept;
    [[nodiscard]] agent::Endpoint* endpoint() noexcept;
    [[nodiscard]] std::size_t frames_shown() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui
