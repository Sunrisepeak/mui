// openmui:app — an application, and the ways to run it (component-binding
// §10.2): a window by default; `--headless` for CI (frames on a virtual
// clock, a PNG); `--serve` to keep a headless session open for agents.
export module omui:app;

import std;
import mcpplibs.cmdline;
import omui.core;
import omui.view;
import omui.agent;
import omui.ui.window;

export namespace omui {

struct VerbSpec {
    VerbDesc desc;
    VerbFn   fn;
};

// The self-check agent.md gives an agent once it is linked (agent connect
// §5.2): activate one node, and another's name then says `expect`.
struct AgentCheck {
    std::string activate;
    std::string read;
    std::string expect;
};

struct AppSpec {
    std::string            id = "openmui-app";   // reverse-DNS: org.example.notes
    std::string            title = "openmui";
    Size                   size{960, 640};
    std::function<View()>  view;                 // what the window shows (mounted at "main")
    std::vector<VerbSpec>  verbs;                // the application's verbs (S-1: their effects classify actions)
    Theme                  theme = Theme::light();
    // A directory of the application's own effects (*.effect.omui.ts, or their
    // JSON): loaded with the window, loaded again when one changes
    // (openmui.render.effect). A module is read through `effect_data`.
    std::filesystem::path  effects;
    std::function<std::expected<std::string, std::string>(const std::filesystem::path&)> effect_data;
    // Called once the runtime exists, before the view mounts (timers, more slots).
    std::function<void(Runtime&)> setup;
    // Called before the runtime goes, whichever way the run ends: what setup
    // made that holds on to the runtime (a player, its timers) lets go here.
    std::function<void(Runtime&)> teardown;
    // Whether what setup started (scripts on their threads) is done for now:
    // a headless run settles until it is.
    std::function<bool()> ready;
    // The application's languages (D-74): the language its keys are written
    // in, a directory of catalogs (`<tag>.messages.omui.toml`), and catalogs
    // carried in the program (tag, text). CLDR's packs come with openmui.
    std::string            source_language = "en";
    std::filesystem::path  messages;
    std::vector<std::pair<std::string, std::string_view>> catalogs;
    // What agents may do over the bus (spec S-11, openas §5): the cap —
    // "none", "read", "reversible", "irreversible" or "external" — and the
    // grants the person gave before (openas's text form, from the
    // application's settings). The person gives more through the
    // application's own interface: Runtime::agent_access().give(…).
    std::string              agent = "external";
    // Whether agents may also give input as a person's devices would —
    // point, click, drag, scroll, type (the protocol's `input`) — what it
    // does decided as their requests are (S-10, S-11). On by default, as
    // the endpoint is; --no-agent-input turns it off for a run.
    bool                     agent_input = true;
    // Whether agents may join as players where a game offers it (plan §8.4:
    // the application allows; --no-agent-player turns it off for a run).
    bool                     agent_players = false;
    std::vector<std::string> grants;
    // Deployment mode is independent of the effect cap above. Automatic
    // consumes the mcpp build profile: dev is development, others dual-user.
    AgentMode                agent_mode = AgentMode::automatic;
    // Settings supplied by the developer and the person; session limits
    // belong to the trusted Protocol/provider session, never request JSON.
    AgentRestrictions        agent_developer, agent_user;
    // Whether the platform's assistive technology is served the tree (a
    // screen reader, a magnifier, voice control: UI Automation,
    // NSAccessibility, AT-SPI). On by default; --no-accessibility turns it
    // off for a run.
    bool                     accessibility = true;
    // Who draws the window's frame (D-75): "native" — the system (the
    // default); "framework" — the interface (window_frame, its look in
    // `frame`, its title `title` unless set); "switchable" — the system's,
    // and the person may switch (the verb window.frame). --window overrides.
    std::string              window = "native";
    WindowFrameOptions       frame;
    // Who draws its frames, on the desktop and in a page: "automatic" (a
    // hardware GPU when there is one, else the CPU), "cpu" — the reference,
    // the same bytes on every machine — or "gpu" (insisted on). --gpu and
    // --cpu override it.
    std::string              renderer = "automatic";
    // What agent.md says of it (`--agent-guide`): a line on what it is, and
    // the self-check an agent does once linked.
    std::string              description;
    AgentCheck               agent_check;
    // Its own short skill (markdown): what an agent should know beyond its
    // verbs and places — a figure that is its body on the page, a step that
    // needs the person — under "This application" in agent.md.
    std::string              agent_skill;
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
//   --agent-name NAME         the endpoint's name (default: the application id; another for a second copy)
//   --agent-mode MODE         trusted startup preset: automatic, development, dual-user
//   --no-agent                no endpoint
//   --no-agent-input          agents act on nodes only, giving no input as a person's devices would
//   --agent-input             they may (the default unless the application turns it off)
//   --no-accessibility        the tree not served to the platform's assistive technology
//   --dark                    the dark theme
//   --gpu | --cpu             who draws the window (default: the GPU when the machine has one)
//   --reduce-transparency     see-through materials (glass) drawn as opaque panels
//   --size WxH                the window's size in logical pixels
//   --debug LIST              overlays: bounds,repaints,inspector,hud (the runtime's DebugOptions)
//   --log FILTER              log levels per category: layout=debug,*=info (also OMUI_LOG)
//   --profile FILE            record a trace of the run (Chrome / Perfetto JSON)
//   --lang TAGS               the person's languages, best first: zh-CN,en (default: the system's)
//   --window MODE             who draws the window's frame: native, framework, switchable (default: the application's)
//   --agent-guide DIR         write what an agent needs to operate it — agent.md, .well-known/openmui-agent.json — into DIR, and link its page there
struct RunOptions {
    bool                  headless = false;
    bool                  serve = false;
    std::optional<int>    frames;
    std::filesystem::path png;
    int                   level = 2;
    std::filesystem::path agent_dir;
    std::string           agent_name;  // --agent-name
    bool                  agent = true;
    std::optional<AgentMode> agent_mode; // trusted startup override, not a remote field
    std::optional<bool>   agent_input;  // --agent-input / --no-agent-input (default: the application's)
    bool                  no_agent_player = false;  // --no-agent-player: no agent joins as a player
    bool                  accessibility = true;  // --no-accessibility
    bool                  dark = false;
    std::optional<bool>   gpu;  // --gpu / --cpu
    bool                  reduce_transparency = false;
    std::optional<Size>   size;
    DebugOptions          debug;
    std::string           log;
    std::filesystem::path profile;
    std::vector<std::string> languages;  // --lang
    std::string           window;          // --window native|framework|switchable
    std::filesystem::path agent_guide;     // --agent-guide DIR (a headless run that writes it, then ends)
    std::filesystem::path replay;          // --replay FILE: a recording (ui.record) played headless, its end checked (W12)

    // The switches above, declared on a program's command line (mcpplibs.cmdline,
    // D-56): a program with switches of its own declares them beside these on
    // the same App, parses once, and reads these back with from().
    static void declare(mcpplibs::cmdline::App& app);
    // What the switches say; a value an option does not take is refused.
    [[nodiscard]] static std::expected<RunOptions, std::string> from(const mcpplibs::cmdline::ParsedArgs& parsed);
    // Both, over the arguments after the program's name (a program with none of its own, tests).
    [[nodiscard]] static std::expected<RunOptions, std::string> parse(std::span<const std::string_view> args);
};

// Build metadata is generated by mcpp, not guessed from assertions/macros.
[[nodiscard]] std::string_view application_build_profile() noexcept;
// Resolve the reusable core preset and the application's narrowing layers.
// Outer omui host/provider adapters can reuse core's agent_policy_for_profile.
// Independent base/gfx engines cannot depend on this UI package; extracting
// their neutral control/preset contract remains separate work.
[[nodiscard]] std::expected<AgentPolicy, std::string> application_agent_policy(
    const AppSpec& app, std::optional<AgentMode> startup_mode = {});

// Who draws, as the person chooses it while the application runs:
// "automatic", "gpu" or "cpu". On the Web the page loads again with
// ?renderer=NAME, which it then follows over the application's choice; on
// the desktop the choice is the next start's (--gpu, --cpu), and the answer
// says so.
std::expected<void, std::string> choose_renderer(std::string_view name);

// A program's command line: its own switches, declared on `line`, beside
// RunOptions's, parsed once (mcpplibs.cmdline, D-56). --help says them all and
// ends with 0; a mistake is said on standard error and ends with 2 — the
// error is the exit code to end with.
//
//     mcpplibs::cmdline::App line{"md-stream"};
//     (void)line.option("rate").takes_value().value_name("N").help("pieces a second");
//     auto command = omui::command_line(std::move(line), argc, argv);
//     if (!command) return command.error();
//     const double rate = command->args.value_as<double>("rate").value_or(30);
//     return omui::run(app, command->options);
struct CommandLine {
    mcpplibs::cmdline::ParsedArgs args;
    RunOptions                    options;
};
[[nodiscard]] std::expected<CommandLine, int> command_line(mcpplibs::cmdline::App line, int argc, char** argv);

// Runs the application as the options say; answers the process's exit code.
int run(AppSpec app, const RunOptions& options);
int run(AppSpec app, int argc, char** argv);

}  // namespace omui
