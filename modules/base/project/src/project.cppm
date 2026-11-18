// mlibs.project — a project as the tools see it, and its health (D-205's project model and doctor, D-207).
//
//     auto project = mlibs::project::read(".");                                  // its packages, from the manifests
//     auto findings = mlibs::project::check(*project, {mlibs::project::general(), mlibs::project::workspace()});
//
// A profile is a set of rules over the model; the general rules hold for every project, a domain's owner adds
// its own (omui: an application, an extension; hanim: a film; meng: a game). A package says its profile in
// `[package.metadata.openmui] profile = "app"`; a rule of a profile looks only at packages that say it.
export module mlibs.project;

import std;

export namespace mlibs::project {

struct Package {
    std::filesystem::path dir;           // absolute
    std::string relative;                // from the project's root, `/`-separated ("" for the root package)
    bool other_repository = false;       // a submodule: another repository's, kept by its own rules
    std::string name;                    // "omui.example.hello-ts": namespace and name
    std::string profile;                 // [package.metadata.openmui] profile, or `app` for `kind = "app"`, or ""
    bool library = false;                // it builds a library (a `lib` target, or no target named): others import it
    std::vector<std::string> dependencies;  // every dependency's key, of every kind (dotted names as written)
    std::vector<std::string> path_dependencies;  // relative to the root, `/`-separated
    std::vector<std::string> files;      // the package's own files, from its directory, `/`-separated (not nested packages')
    std::map<std::string, std::string> metadata;  // [package.metadata.openmui]'s plain values
};

struct Project {
    std::filesystem::path root;
    bool workspace = false;            // the root manifest has [workspace]
    std::vector<std::string> members;  // as it lists them ("dir/*" expanded)
    std::vector<Package> packages;     // the root's package (if any) and every member's
    std::vector<std::string> problems; // a manifest that is not TOML, a member without one
};

// The project at `root`: its manifest, and its members' when it is a workspace.
[[nodiscard]] std::expected<Project, std::string> read(const std::filesystem::path& root);

struct Finding {
    std::string rule;   // "general.manifest", "app.identity" …
    std::string where;  // a path from the root
    std::string text;   // what is wrong
    std::string fix;    // what to do
};

struct Rule {
    std::string id;
    std::string doc;
    std::function<void(const Project&, const Package&, std::vector<Finding>&)> check;
};

struct Profile {
    std::string name;  // "general", "workspace", "app" …; a package's `profile` selects one (general: every package)
    std::string doc;
    std::vector<Rule> rules;
};

// The rules every project keeps (D-207): one project file (mcpp.toml), a package's directory is its name, its module
// is its name, tests in tests/ with mtest, data files named <name>.<kind>.xdef.<ts|json|jsonl>.
[[nodiscard]] Profile general();
// A workspace's: every member listed has a manifest, and every manifest under the root is listed.
[[nodiscard]] Profile workspace();

// Profiles as their owners declare them (D-207): a `*.profile.xdef.json` snapshot, `{"$format":"profile@1",
// "profile": {<name>: {doc, rules: [{id, doc, fix, <condition>}]}}}`. A condition is `metadata: [keys]` (all said in
// [package.metadata.openmui]), `file: "glob"` (a file of the package matches; `*` within a directory, `**` across),
// `dependsOn: "package"`, or `anyOf: [conditions]`; a rule holds when its condition does.
[[nodiscard]] std::expected<std::vector<Profile>, std::string> profiles_from(std::string_view json, std::string_view file);
// The profiles a tree declares: every `*.profile.xdef.json` under `root` (build trees and submodules aside).
[[nodiscard]] std::expected<std::vector<Profile>, std::string> profiles_in(const std::filesystem::path& root);

// Every rule of every profile over the project: the general ones and workspace's over all packages, another
// profile's over the packages that say it.
[[nodiscard]] std::vector<Finding> check(const Project& project, std::span<const Profile> profiles);

}  // namespace mlibs::project
