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

// What the tree of the project under `project` (a directory, "modules/projects/omui") holds, or why not.
[[nodiscard]] std::expected<Plan, std::string> plan(const std::filesystem::path& root, std::string_view project);
// The tree written at `out` (made empty first): the files, and a root manifest listing the members.
[[nodiscard]] std::expected<void, std::string> write(const std::filesystem::path& root, const Plan& plan, const std::filesystem::path& out);

}  // namespace omui::devtools::split
