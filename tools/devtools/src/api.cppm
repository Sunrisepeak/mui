// omui.devtools.api — a package's exported surface as text: what its module interface units export (declarations,
// the public members of the classes they export, re-exported modules), function bodies and initialisers left out,
// one declaration a line, whitespace made one space. Committed beside a package as `api.omui.txt`, it is the
// surface its label promises (release plan §2.1: a stable package's API is v1): a change to it is a change to that
// file in the same commit, and the devtools' test holds every committed file to its package.
//
// It reads the text, not the compiler's model: comments and bodies are dropped by their braces, a class's
// private and protected members are left out, a partition counts when its unit exports anything.
export module omui.devtools.api;

import std;

export namespace omui::devtools::api {

// One interface unit's exported declarations, in order (indented two spaces a class deep).
[[nodiscard]] std::vector<std::string> exported(std::string_view unit);

// A package's surface: each interface unit (`*.cppm` that declares `export module`) under `sources`, by its module
// name, then its declarations; the units in the order of their module names.
[[nodiscard]] std::string surface(const std::filesystem::path& member, std::span<const std::string> sources);

// The interface units of a member as its manifest's [build] sources name them (the `*.cppm` globs).
[[nodiscard]] std::vector<std::string> interface_globs(std::string_view manifest);

}  // namespace omui::devtools::api
