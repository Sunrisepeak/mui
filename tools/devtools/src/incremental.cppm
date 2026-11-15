// omui.devtools.incremental — a repeated mcpp build checked against its actual
// compilation outputs: the first build warms the selected graph, and the same
// build again must leave every compilation output it has untouched. This
// catches a generated source that invalidates every importer even when its
// bytes do not change. Wall time is reported, never a pass or fail: it is the
// runner's.
export module omui.devtools.incremental;

import std;

export namespace omui::devtools::incremental {

struct Options {
    std::vector<std::string> packages;  // -p each; or the workspace
    bool                     workspace = false;
    std::vector<std::string> excluded;  // with the workspace
    std::string              profile = "dev";
    std::string              toolchain;  // empty: the default
    bool                     locked = false;
    std::filesystem::path    report = "target/incremental.report.omui.json";
    std::filesystem::path    mcpp = "mcpp";  // MCPP, or mcpp on PATH
};

// The mcpp arguments the check builds with (after the program).
[[nodiscard]] std::vector<std::string> arguments(const Options& options);

// The build configurations' directories a build's JSON events name (its `plan` events).
[[nodiscard]] std::set<std::filesystem::path> configurations(std::string_view events);

// A compilation output and what says it changed.
struct Stamp {
    std::int64_t  modified = 0;  // nanoseconds, the file system's
    std::uintmax_t size = 0;
    friend bool operator==(const Stamp&, const Stamp&) = default;
};
struct Outputs {
    std::map<std::string, Stamp> found;
    std::set<std::string>        missing;  // named by a database without a file (a module served from a cache)
};
// The outputs `compile_commands.json` in each directory names, as they are now.
[[nodiscard]] std::expected<Outputs, std::string> outputs(const std::set<std::filesystem::path>& directories);

// The outputs whose stamp differs between two looks (one that is in one only, too).
[[nodiscard]] std::vector<std::string> changed(const Outputs& before, const Outputs& after);

// The whole check: both builds, the report written (JSON), a line said. Whether it holds.
[[nodiscard]] bool check(const Options& options, std::ostream& said, std::ostream& errors);

}  // namespace omui::devtools::incremental
