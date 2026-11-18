// mlibs.build.plan — what build plugins share (D-73): a planned edge, handing edges to the build, a tool built for
// the build, a path's text and the files a plugin reads. Each plugin re-exports it; omui's build plugins too.
export module mlibs.build.plan;

import std;
import mcpp;

export namespace mlibs::build {

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

// Hands the edges to the build as source actions: C++ the package compiles, so every compile edge waits for them.
bool submit(std::span<const edge> edges);

// A program a dependency builds for the build (`tools = ["…"]` on it): empty when it was not.
[[nodiscard]] std::string tool(std::string_view package, std::string_view program);

// UTF-8 text of a path (generic separators for `generic_utf8`), never through the process's code page:
// std::filesystem::path::string() throws on Windows for a name the ANSI code page cannot spell (lint `paths`).
[[nodiscard]] std::string utf8(const std::filesystem::path& p);
[[nodiscard]] std::string generic_utf8(const std::filesystem::path& p);

// Whether this package is the one `mcpp pack` is packing (true when the engine does not say). Packing a program is
// that package's alone; the stage directory names it (<name>-<version>-<target>), since mcpp hands each workspace
// member a graph rooted at itself.
[[nodiscard]] bool is_packed();

// A module's C++ namespace: acme.ui → acme::ui.
[[nodiscard]] std::string namespace_of(std::string_view module);

// The files in `dir` whose names end in `suffix`, sorted (none: no directory).
[[nodiscard]] std::vector<std::filesystem::path> files_ending(const std::filesystem::path& dir, std::string_view suffix);

// Every file under `dir`, recursively, sorted, as UTF-8 text.
[[nodiscard]] std::vector<std::string> files_under(const std::filesystem::path& dir);

}  // namespace mlibs::build
