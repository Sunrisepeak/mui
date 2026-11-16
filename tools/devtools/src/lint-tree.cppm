// What the lint's rules share (internal to omui.devtools.lint): the tree's
// files and the workspace's manifests, read once, and each group's check.
module omui.devtools.lint:tree;

import std;
import tomlplusplus;
import :problem;

namespace omui::devtools::lint {

std::optional<std::string> read_text(const std::filesystem::path& path);
std::filesystem::path path_of(std::string_view utf8);  // a "/" path from the root, as the file system's
std::string utf8_of(const std::filesystem::path& path);  // the other way, "/" separated

// A member: its path from the root, its manifest's text, and the manifest read (absent when it is not TOML).
struct Member {
    std::string path;
    std::string text;
    std::optional<toml::table> data;
    std::string error;  // why it is not TOML, with its line
};

struct Workspace {
    std::filesystem::path root;
    std::vector<Member> members;  // in the root manifest's order; listed ones without a manifest have no text
    std::optional<toml::table> root_data;
    std::string root_error;

    static Workspace read(const std::filesystem::path& root);
    [[nodiscard]] const Member* find(std::string_view path) const;
};

// A dependency table's entries with their names joined again (`compat.sdl3`): a table that is not
// a dependency's own (no path, git, version or workspace key) holds names below it.
std::vector<std::pair<std::string, const toml::node*>> flatten(const toml::table& table, std::string prefix = {});

// The member a path dependency names, from the root ("" when it lies outside).
std::string resolved(std::string_view member, std::string_view relative);

// The members whose results are part of A-level conformance (identical bits).
std::span<const std::string_view> deterministic_members() noexcept;

// The groups' checks.
std::vector<Problem> check_manifests(const Workspace& workspace);  // toml, members, manifest, layering, modules, determinism's flags
std::vector<Problem> check_projects(const Workspace& workspace);   // project-dag
std::vector<Problem> check_layout(const Workspace& workspace);     // layout

}  // namespace omui::devtools::lint
