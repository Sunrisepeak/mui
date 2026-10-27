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
//
// Frames are drawn by the GPU renderer into a texture the window's swapchain
// shows (omui.rhi on SDL_GPU), or by the CPU renderer into a surface
// streamed to the window — the same pixels (openmui.render's tests hold the
// two together). A directory of effect files (openmui.effect) is loaded with
// the window and loaded again, between frames, when a file in it changes:
// an effect's shader edited while the program runs shows on the next frame.
export module openmui.host.sdl3;

import std;
export import openmui.core;
export import openmui.view;
export import openmui.semantic;
export import openmui.agent;
export import openmui.raster;
export import openmui.effect;

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
    bool                    reduce_transparency = false;  // glass drawn as opaque panels
    Color                   background = Color::white();
    bool                    vsync = true;
    std::string             video_driver;     // "" the platform's; "offscreen" in CI
    std::string             render_driver;    // "" the best; "software" for exact read-back
    bool                    parallel_raster = true;  // the CPU renderer draws bands of rows at the same time
    // Who draws the frames: the GPU when the machine has one (a software
    // rasteriser such as lavapipe or WARP is slower than the CPU renderer),
    // else the CPU; or either, insisted on (gpu: no device is an error).
    enum class Renderer : std::uint8_t { automatic, cpu, gpu };
    Renderer                renderer = Renderer::automatic;
    std::string             vulkan_library;   // the Vulkan loader the GPU device uses ("" the system's)
    // A directory of *.effect.omui.toml (openmui.effect): loaded with the
    // window, loaded again when a file changes. Effects are drawn by the GPU
    // renderer; the CPU renderer draws their layers without them.
    std::filesystem::path   effects;
    // Show a runtime that exists already rather than make one (the window
    // does not own it; it must outlive the desktop).
    Runtime*                runtime = nullptr;
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
    // Whether the GPU renderer draws the frames (after open()).
    [[nodiscard]] bool gpu() const noexcept;
    // The effect library of `effects` (null: none asked for).
    [[nodiscard]] effect::Library* effects() noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui
