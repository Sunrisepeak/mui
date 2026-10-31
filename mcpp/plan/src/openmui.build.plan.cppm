// openmui.build.plan — what openmui's build plugins share: a planned edge,
// handing edges to the build, the generator, and the files a plugin reads.
// Each plugin is a package of its own, for one job, and a package's build
// program imports only the ones it uses (mcpp/README.md); each re-exports
// this module.
export module openmui.build.plan;

import std;
import mcpp;

export namespace openmui::build {

// One planned edge, so a build program can adjust it before submitting.
struct edge {
    std::string              id;
    std::string              description;
    std::vector<std::string> command;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> provides;  // a generated module interface's module
    std::vector<std::string> imports;   // what a generated unit imports (it cannot be scanned before it exists)
};

// Hands the edges to the build as source actions: C++ the package compiles,
// so every compile edge waits for them.
bool submit(std::span<const edge> edges);

// UTF-8 text of a path (generic separators for `generic_utf8`), never
// through the process's code page: std::filesystem::path::string() throws on
// Windows for a name the ANSI code page cannot spell (lint rule `paths`).
[[nodiscard]] std::string utf8(const std::filesystem::path& p);
[[nodiscard]] std::string generic_utf8(const std::filesystem::path& p);

// omui-gen as built for this build (empty: it was not — the plugin's package
// depends on openmui.gen with tools = ["omui-gen"]).
[[nodiscard]] std::string generator();

// Whether this package is the one `mcpp pack` is packing (true when the
// engine does not say). Packing a program is that package's alone: a package
// it depends on leaves its own packing alone. The stage directory names it
// (<name>-<version>-<target>); the graph cannot, since mcpp hands each
// workspace member a graph rooted at itself.
[[nodiscard]] bool is_packed();

// A module's C++ namespace: acme.ui → acme::ui.
[[nodiscard]] std::string namespace_of(std::string_view module);

// The files in `dir` whose names end in `suffix`, sorted (none: no directory).
[[nodiscard]] std::vector<std::filesystem::path> files_ending(const std::filesystem::path& dir, std::string_view suffix);

// Every file under `dir`, recursively, sorted, as UTF-8 text.
[[nodiscard]] std::vector<std::string> files_under(const std::filesystem::path& dir);

// The names a TOML file declares as `[<table>.<name>]` (a deeper
// `[<table>.<name>.<part>]` is a sub-table, not a name).
[[nodiscard]] std::vector<std::string> table_names(const std::filesystem::path& file, std::string_view table);

}  // namespace openmui::build
