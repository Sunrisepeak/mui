// omui-devtools release — the release channel (RC3.1): the packages a new project needs, read from
// the templates' manifests and followed through every member they reach by `path` (all their
// dependency tables but the development ones, every feature and platform: what `mcpp publish` must
// find published); `release index` has `mcpp publish --dry-run` make each one's archive and mcpp-index
// descriptor into a directory for review. Nothing is published.
export module omui.devtools.release;

import std;

export namespace omui::devtools::release {

// Where the templates are, and the package `mcpp new --template` takes them from: their
// `{{self.name}}`.
inline constexpr std::string_view templates_directory = "modules/projects/omui/sdk/templates";
inline constexpr std::string_view template_package = "omui.sdk";

struct Package {
    std::string name;      // omui.core
    std::string path;      // modules/projects/omui/core, from the root
    std::string version;   // its [package] version
    std::string maturity;  // stable, preview, experimental (empty: not labelled)
    std::string description, license, repo;
    bool built = false;  // reached with the features a template's project builds with (else: only
                         // by another feature or the publish rule)
    std::vector<std::string>
        outside;  // other strings in its manifest or build.mcpp that leave its directory ("../…")
    std::vector<std::string> external;  // its version dependencies, `name@version`
};

struct Closure {
    std::vector<std::string> roots;  // the members the templates name
    std::vector<Package> packages;   // the closure, in the workspace's order
    std::vector<std::string>
        external;  // every version dependency of the closure and the templates, `name@version`
    std::vector<std::string> problems;  // a path that is no member, a template that is not TOML
};

// The closure of the templates under `root` (a checkout of the repository).
[[nodiscard]] std::expected<Closure, std::string> closure(const std::filesystem::path& root);

// The descriptor's place in mcpp-index: pkgs/<initial of the full name>/<full name>.lua. The descriptor itself, the
// archive and its sum are `mcpp publish`'s (2026.11.17.2: a member's archive by its identity, siblings at their own
// versions, mcpp#947).
[[nodiscard]] std::string descriptor_path(const Package& package);

// `name@version` of the external dependencies that the index under `index` (pkgs/<x>/*.lua) does
// not carry.
[[nodiscard]] std::vector<std::string> missing_from(const std::filesystem::path& index,
                                                    std::span<const std::string> external);

// Each member's package name and its path from the root.
[[nodiscard]] std::map<std::string, std::string> member_paths(const std::filesystem::path& root);

// A rendered template's manifest pointed at a checkout: each dependency on a member, written as the
// published version (`omui.theme = "0.1.0"`, `omui.build = { version = "0.1.0", host-module = true
// }`), becomes that member's path under `checkout`; everything else is kept. The template states
// what a user's project states; this is the one step that makes it build against this tree (`check
// templates`).
[[nodiscard]] std::string pointed_at(std::string_view manifest,
                                     const std::map<std::string, std::string>& members,
                                     std::string_view checkout);

}  // namespace omui::devtools::release
