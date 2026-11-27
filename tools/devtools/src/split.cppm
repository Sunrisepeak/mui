// omui-devtools split — a project as a repository of its own (RC3.13, D-55, D-161): the project's members and every
// member they reach by `path` (their tests' too), copied with the repository's root files into a tree whose root
// manifest lists only them. Building and testing that tree is the drill: whatever a project needs and does not
// declare is missing there.
export module omui.devtools.split;

import std;

export namespace omui::devtools::split {

struct Plan {
    std::vector<std::string> members;  // the project's and those they reach, in the workspace's order
    std::vector<std::string> files;    // every tracked file the tree holds, from the root
};

// The tiers (D-231), each a repository of its own one day; a tree of one holds it and the tiers below it.
inline constexpr std::array<std::string_view, 3> tiers{"foundation", "framework", "engine"};

// What the tree of the project under `project` (a directory, "modules/projects/omui") holds, or why not. `project`
// may name a tier instead: mlibs' groups and the projects whose identity names that tier or one below, the
// repository's maintenance tools with the foundation; refused when what they reach holds a member of a tier above.
[[nodiscard]] std::expected<Plan, std::string> plan(const std::filesystem::path& root, std::string_view project);
// The tree written at `out` (made empty first): the files, and a root manifest listing the members.
[[nodiscard]] std::expected<void, std::string> write(const std::filesystem::path& root, const Plan& plan, const std::filesystem::path& out);

}  // namespace omui::devtools::split
