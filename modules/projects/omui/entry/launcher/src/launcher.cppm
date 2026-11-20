// omui.entry.launcher — the shared runtime's launcher (E3b, D-250). An
// application declares in its manifest what it needs of a runtime:
//
//     [package.metadata.openmui.runtime]
//     openmui = "0.4"          # the specification's version: any runtime of that major and minor
//     needs   = ["agent"]      # the capabilities it uses
//     runtime = "=0.1.0"       # optional: this runtime exactly
//
// Each runtime version is installed in a directory of its own (xlings:
// <home>/data/xpkgs/xim-x-omui-runtime/<version>/) and says what it is in its
// runtime.json (runtime@1, `omui-player --runtime`). The launcher starts the
// newest that satisfies the declaration; with none, it says why each installed
// one does not and the command that installs one that would.
export module omui.entry.launcher;

import std;

export namespace omui::launcher {

// What an application declares of its runtime.
struct Declared {
    std::string              openmui;  // "0.4"
    std::vector<std::string> needs;
    std::string              runtime;  // "=0.1.0", or empty
};
// The application's manifest's [package.metadata.openmui.runtime], or what is wrong with it.
[[nodiscard]] std::expected<Declared, std::string> declared(std::string_view manifest);

// A runtime installed: where, and what it says it is.
struct Runtime {
    std::filesystem::path    directory;  // its runtime.json's
    std::string              version, openmui;
    std::vector<std::string> capabilities;
};
// runtime.json's text read (runtime@1).
[[nodiscard]] std::expected<Runtime, std::string> read_runtime(std::string_view text, const std::filesystem::path& directory);
// Every runtime under an xlings home (data/xpkgs/xim-x-omui-runtime/<version>/, its runtime.json there or a level below).
[[nodiscard]] std::vector<Runtime> installed(const std::filesystem::path& xlings_home);

// The runtime that starts it, or why none: each installed one's reason, and the command that installs one that would.
struct Choice {
    std::optional<Runtime> chosen;
    std::string            why;      // with none chosen
    std::string            install;  // the command that installs one: `xlings install omui-runtime@…`
};
[[nodiscard]] Choice choose(const Declared& wanted, std::span<const Runtime> runtimes);

// Versions compared as numbers, part by part ("0.1.10" > "0.1.9"); a pre-release ("0.1.0-dev.3") before its release.
[[nodiscard]] std::strong_ordering compare_versions(std::string_view left, std::string_view right);

inline constexpr std::string_view launcher_version = "0.1.0";

// What to start: the application (its directory or mcpp.toml; empty: the working directory), whether only to say the
// runtime it would take (`--which`), and what its player is given after `--manifest`.
struct Launch {
    std::filesystem::path    application;
    bool                     which = false;
    std::vector<std::string> passed;
};
// The application started by its runtime's player (this program becomes it); 2 when no installed runtime satisfies
// it (said, with the command that installs one), 1 when the player could not start.
int launch(const Launch& what);

// `omui-launch [--which] [APP] [-- ARGS]` (mcpplibs.cmdline): its exit code.
int main(int argc, char** argv);

}  // namespace omui::launcher
