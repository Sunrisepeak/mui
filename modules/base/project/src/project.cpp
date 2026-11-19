// mlibs.project: the general rules, the workspace's, and the check that runs a set of profiles.
module mlibs.project;

import std;
import mcpplibs.os;
import mlibs.xdef;

namespace mlibs::project {

namespace {

std::string text_of(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::string where(const Package& package, std::string_view file = {}) {
    if (file.empty()) return package.relative.empty() ? std::string("mcpp.toml") : package.relative + "/mcpp.toml";
    return package.relative.empty() ? std::string(file) : std::format("{}/{}", package.relative, file);
}

std::string last_segment(std::string_view name) { return std::string(name.substr(name.rfind('.') == std::string_view::npos ? 0 : name.rfind('.') + 1)); }

std::string module_form(std::string text) {
    std::ranges::replace(text, '-', '_');
    return text;
}

bool is_source(std::string_view file) { return file.ends_with(".cpp") || file.ends_with(".cppm") || file.ends_with(".cc"); }

}  // namespace

Profile general() {
    Profile profile{"general", "what every project keeps, whatever its domain (D-207)", {}};
    profile.rules.push_back({"general.manifest", "a package's one project file is mcpp.toml", [](const Project&, const Package& package, std::vector<Finding>& out) {
        constexpr std::array others{"CMakeLists.txt", "meson.build", "package.json", "Makefile", "xmake.lua", "BUILD.bazel", "BUILD", "conanfile.txt",
                                    "conanfile.py", "vcpkg.json", "premake5.lua", "SConstruct"};
        for (const auto& file : package.files)
            if (std::ranges::contains(others, file))
                out.push_back({"general.manifest", where(package, file), std::format("{} is another build system's project file", file),
                               "describe the package in mcpp.toml alone (dependencies, targets, features); tools in [xlings.workspace]"});
    }});
    profile.rules.push_back({"general.path-name", "a member's directory is its name's last part", [](const Project&, const Package& package, std::vector<Finding>& out) {
        if (package.relative.empty() || package.name.empty() || package.other_repository) return;
        std::string leaf = mcpplibs::os::fs::utf8(package.dir.filename());
        // An ordering prefix is not part of the name (`02-hello-ts` is hello-ts, the second of a list).
        if (const auto dash = leaf.find('-'); dash != std::string::npos && dash > 0 && std::ranges::all_of(leaf.substr(0, dash), [](char c) { return c >= '0' && c <= '9'; }))
            leaf = leaf.substr(dash + 1);
        if (module_form(leaf) != module_form(last_segment(package.name)))
            out.push_back({"general.path-name", where(package), std::format("the package {} is in {}/", package.name, leaf),
                           std::format("name the directory {} or the package after its directory", last_segment(package.name))});
    }});
    profile.rules.push_back({"general.module-name", "a library's module is its package's name", [](const Project&, const Package& package, std::vector<Finding>& out) {
        if (package.name.empty() || !package.library || package.other_repository) return;  // an application's modules are its own
        std::vector<std::string> exported;
        for (const auto& file : package.files) {
            if (!file.ends_with(".cppm") || file.starts_with("tests/")) continue;
            const std::string text = text_of(package.dir / mcpplibs::os::fs::from_utf8(file));
            for (std::size_t at = text.find("export module "); at != std::string::npos; at = text.find("export module ", at + 1)) {
                const auto end = text.find_first_of(";:", at + 14);
                if (end != std::string::npos) exported.push_back(text.substr(at + 14, end - at - 14));
            }
        }
        if (exported.empty() || std::ranges::contains(exported, module_form(package.name))) return;
        // A package whose primary module is another name says it (`primary-module = "omui"`, the SDK's umbrella), or
        // that it is a collection of small tools, each its own module (`primary-module = "*"`).
        if (const auto said = package.metadata.find("primary-module"); said != package.metadata.end() && (said->second == "*" || std::ranges::contains(exported, said->second)))
            return;
        out.push_back({"general.module-name", where(package), std::format("no interface is `export module {};` (it exports {})", module_form(package.name), exported.front()),
                       "a package's primary module is its name; if it is another (an umbrella) or a collection, say so: [package.metadata.openmui] primary-module = \"<module>\" or \"*\""});
    }});
    profile.rules.push_back({"general.tests", "tests are in tests/ and written with mtest", [](const Project&, const Package& package, std::vector<Finding>& out) {
        for (const auto& file : package.files) {
            if (!is_source(file)) continue;
            const bool in_tests = file.starts_with("tests/");
            const std::string leaf = file.substr(file.rfind('/') == std::string::npos ? 0 : file.rfind('/') + 1);
            if (!in_tests && (leaf.ends_with("_test.cpp") || leaf.ends_with("_tests.cpp")))
                out.push_back({"general.tests", where(package, file), "a test outside tests/", "move it into tests/ (mcpp test finds it there)"});
            if (!in_tests) continue;
            const std::string text = text_of(package.dir / mcpplibs::os::fs::from_utf8(file));
            for (const std::string_view other : {"<gtest/gtest.h>", "<catch2/", "<doctest/doctest.h>", "<boost/test/"})
                if (text.starts_with(std::format("#include {}", other)) || text.contains(std::format("\n#include {}", other)))
                    out.push_back({"general.tests", where(package, file), std::format("a test written with another framework ({})", other),
                                   "write it with mtest (import mcpplibs.mtest; UTest cases{…})"});
        }
    }});
    profile.rules.push_back({"general.data-names", "a declared data file is <name>.<kind>.xdef.<ts|json|jsonl>", [](const Project&, const Package& package, std::vector<Finding>& out) {
        for (const auto& file : package.files) {
            const std::string leaf = file.substr(file.rfind('/') == std::string::npos ? 0 : file.rfind('/') + 1);
            if (const auto name = mlibs::xdef::parse_name(leaf); name && name->container != mlibs::xdef::container) {
                out.push_back({"general.data-names", where(package, file), std::format("named with the earlier suffix (.{}.)", name->container),
                               "xdef migrate renames it and every text that names it"});
                continue;
            }
            // A tool's configuration (`<tool>.config.ts`: omui.config.ts) is named for its tool, not as a data file.
            if (!leaf.ends_with(".ts") || leaf.ends_with(".d.ts") || leaf.ends_with(".config.ts") || mlibs::xdef::parse_name(leaf)) continue;
            const std::string text = text_of(package.dir / mcpplibs::os::fs::from_utf8(file));
            if (text.contains("export default define"))
                out.push_back({"general.data-names", where(package, file), "a declared data module not named as one",
                               "name it <name>.<kind>.xdef.ts (its kind is its define function's: defineTheme → theme)"});
        }
    }});
    return profile;
}

Profile workspace() {
    Profile profile{"workspace", "a workspace of packages: what the root lists is what there is (D-207)", {}};
    profile.rules.push_back({"workspace.members", "every listed member has a manifest; every manifest is listed", [](const Project& project, const Package& package, std::vector<Finding>& out) {
        if (!package.relative.empty() || !project.workspace) return;  // once, for the root
        std::error_code error;
        for (const auto& member : project.members)
            if (!std::filesystem::exists(project.root / mcpplibs::os::fs::from_utf8(member) / "mcpp.toml", error))
                out.push_back({"workspace.members", member, "listed and has no mcpp.toml", "give it a manifest or drop it from [workspace] members"});
        for (auto walker = std::filesystem::recursive_directory_iterator(project.root, std::filesystem::directory_options::skip_permission_denied, error);
             walker != std::filesystem::recursive_directory_iterator(); walker.increment(error)) {
            if (error) break;
            const auto leaf = mcpplibs::os::fs::utf8(walker->path().filename());
            if (walker->is_directory(error)) {
                if (leaf.starts_with('.') || leaf == "target" || leaf == "node_modules" || leaf == "fixtures" ||
                    (walker->path() != project.root && std::filesystem::exists(walker->path() / ".git", error)))
                    walker.disable_recursion_pending();
                continue;
            }
            if (leaf != "mcpp.toml" || walker->path().parent_path() == project.root) continue;
            const auto member = mcpplibs::os::fs::generic_utf8(walker->path().parent_path().lexically_relative(project.root));
            if (!std::ranges::contains(project.members, member))
                out.push_back({"workspace.members", member + "/mcpp.toml", "a manifest the workspace does not list", "list it in [workspace] members, or move it out"});
        }
    }});
    return profile;
}

std::vector<Finding> check(const Project& project, std::span<const Profile> profiles) {
    std::vector<Finding> out;
    for (const auto& problem : project.problems) out.push_back({"general.manifest", "mcpp.toml", problem, "make the manifest TOML"});
    // The root is looked at once even when it is a workspace with no package of its own (a rule about the whole
    // project runs for the package whose `relative` is empty).
    std::vector<Package> packages = project.packages;
    if (packages.empty() || !packages.front().relative.empty()) packages.insert(packages.begin(), Package{.dir = project.root});
    for (const auto& profile : profiles)
        for (const auto& package : packages) {
            if (package.name.empty() && !package.relative.empty()) continue;
            const bool everyone = profile.name == "general" || profile.name == "workspace";
            if (!everyone && package.profile != profile.name) continue;
            for (const auto& rule : profile.rules) rule.check(project, package, out);
        }
    return out;
}

}  // namespace mlibs::project
