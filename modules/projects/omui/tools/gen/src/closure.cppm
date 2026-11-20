// omui.gen.closure — what an application carries, derived from what it wrote (B7b; the closure design, D-251): the
// base components its scripts take by name from `openmui` (each module's static imports, read without running it:
// mlibs.ts), in its own files and in every step of the design it replays (a replay runs each step's scripts). The
// result is a value — the names, where each was taken, or every component and why — written as the registry the
// application's program calls (the player's `closure` entry: omui_application_components) and as closure.json.
// Every component is kept where a name cannot be read: a script that takes everything of `openmui` (`import *`,
// `export *`, `import()`), the designer linked (development: an agent may use any component), a generation surface
// open to components.
export module omui.gen.closure;

import std;

export namespace omui::gen {

// A script by its name (a path, or a replay's step: `design/main.replay.xdef.json#3:app/main.ts`) and its text.
struct ClosureSource {
    std::string name;
    std::string text;
};

struct ClosureTaken {
    std::string              file;
    std::size_t              line = 0;
    std::vector<std::string> names;  // as the script wrote them (camelCase)
};

struct Closure {
    bool                      full = false;
    std::string               why;         // why every base component, when full
    std::vector<std::string>  components;  // the contract names (snake_case) taken, sorted, each once
    std::vector<std::string>  natives;     // the official native modules imported (`openmui:native/<name>`), sorted
    std::vector<ClosureTaken> taken;       // where they were taken
};

// The contract name a script's builder stands for: `textBox` → `text_box`.
[[nodiscard]] std::string contract_name(std::string_view builder);

// The closure of these scripts; `full_because`: a reason to keep every component whatever they take (empty: none);
// `kept`: names kept besides, by where they are listed (omui.config.ts's generation.components: what interface made
// while it runs may use). A script that does not read is the error, with where.
[[nodiscard]] std::expected<Closure, std::string> derive_closure(std::span<const ClosureSource> scripts, std::string_view full_because = {},
                                                                 std::span<const ClosureTaken> kept = {});

// What an application's config (omui.config.ts's value, as JSON; `file` names it) asks of its closure: the components
// generation lists (generation.components), or — surfaces open without such a list — every component, and why.
struct ClosureConfig {
    std::string                 full_because;
    std::vector<ClosureTaken>   kept;
};
[[nodiscard]] ClosureConfig closure_config(std::string_view config, std::string_view file);

// The scripts a replay (`replay@1`) runs: each step's texts that are scripts (.ts, .js), named `<replay>#<step>:<path>`.
[[nodiscard]] std::expected<std::vector<ClosureSource>, std::string> replay_scripts(std::string_view replay, std::string_view name);

// The C++ unit: `omui_application_components()`, registering the base components named, markdown when named, and the
// official native modules imported (fuzzy) — or everything official (omui::register_official()).
[[nodiscard]] std::string closure_unit(const Closure& closure);
// closure.json (`closure@1`): what was derived and from where.
[[nodiscard]] std::string closure_report(const Closure& closure);

}  // namespace omui::gen
