// openmui:app — an application, and the ways to run it (component-binding
// §10.2): a window by default; `--headless` for CI (frames on a virtual
// clock, a PNG); `--serve` to keep a headless session open for agents.
export module openmui:app;

import std;
import openmui.core;
import openmui.view;
import openmui.agent;

export namespace omui {

struct VerbSpec {
    VerbDesc desc;
    VerbFn   fn;
};

struct AppSpec {
    std::string            id = "openmui-app";   // reverse-DNS: org.example.notes
    std::string            title = "openmui";
    Size                   size{960, 640};
    std::function<View()>  view;                 // what the window shows (mounted at "main")
    std::vector<VerbSpec>  verbs;                // the application's verbs (S-1: their effects classify actions)
    Theme                  theme = Theme::light();
    // A directory of the application's own effects (*.effect.omui.toml):
    // loaded with the window, loaded again when one changes (openmui.effect).
    std::filesystem::path  effects;
    // Called once the runtime exists, before the view mounts (timers, more slots).
    std::function<void(Runtime&)> setup;
    // Whether what setup started (scripts on their threads) is done for now:
    // a headless run settles until it is.
    std::function<bool()> ready;
};

// How to run, from the command line:
//
//   (nothing)                 a desktop window
//   --headless                no display: frames on a virtual clock
//     --frames N              run N frames (default: until settled)
//     --png FILE              write the last frame
//     --level h0|h1|h2|h3     the headless level (default h2; --serve means h3)
//   --serve                   headless, and keep serving the agent endpoint until closed
//   --agent-dir DIR           where the endpoint listens
//   --no-agent                no endpoint
//   --dark                    the dark theme
//   --gpu | --cpu             who draws the window (default: the GPU when the machine has one)
//   --size WxH                the window's size in logical pixels
//   --debug LIST              overlays: bounds,repaints,inspector,hud (the runtime's DebugOptions)
//   --log FILTER              log levels per category: layout=debug,*=info (also OMUI_LOG)
//   --profile FILE            record a trace of the run (Chrome / Perfetto JSON)
struct RunOptions {
    bool                  headless = false;
    bool                  serve = false;
    std::optional<int>    frames;
    std::filesystem::path png;
    int                   level = 2;
    std::filesystem::path agent_dir;
    bool                  agent = true;
    bool                  dark = false;
    std::optional<bool>   gpu;  // --gpu / --cpu
    std::optional<Size>   size;
    DebugOptions          debug;
    std::string           log;
    std::filesystem::path profile;

    [[nodiscard]] static std::expected<RunOptions, std::string> parse(std::span<const std::string_view> args);
};

// Runs the application as the options say; answers the process's exit code.
int run(AppSpec app, const RunOptions& options);
int run(AppSpec app, int argc, char** argv);

}  // namespace omui
