// A project read from its manifests (mlibs.project): toml++ reports a manifest that is not TOML by throwing, so
// manifests are read here, in a native unit, and the error becomes a problem.
module mlibs.project;

import std;
import mcpplibs.os;
import tomlplusplus;

namespace mlibs::project {

namespace {

constexpr std::array build_trees{"target", ".git", ".mcpp", ".xlings", ".cache", "node_modules", ".venv", "__pycache__", ".omui"};

std::optional<toml::table> parsed(const std::filesystem::path& file, std::string& problem) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        problem = std::format("{}: cannot be read", mcpplibs::os::fs::utf8(file));
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    try {
        return toml::parse(text);
    } catch (const toml::parse_error& failure) {
        problem = std::format("{}:{}: {}", mcpplibs::os::fs::utf8(file), failure.source().begin.line, failure.description());
        return std::nullopt;
    }
}

std::string generic(const std::filesystem::path& path) { return mcpplibs::os::fs::generic_utf8(path); }

// Whether `dir` is another repository's: its own `.git`, or a path the root's `.gitmodules` lists — a submodule checked
// out, or exported without its `.git` (a split tree, an archive), is kept by its own rules all the same.
bool another_repository(const std::filesystem::path& dir, const std::filesystem::path& root) {
    std::error_code error;
    if (dir == root) return false;
    if (std::filesystem::exists(dir / ".git", error)) return true;
    std::ifstream modules(root / ".gitmodules", std::ios::binary);
    const std::string relative = generic(dir.lexically_relative(root));
    for (std::string line; std::getline(modules, line);) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto trim = [](std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
            return text;
        };
        if (trim(std::string_view(line).substr(0, equals)) == "path" && trim(std::string_view(line).substr(equals + 1)) == relative) return true;
    }
    return false;
}

// Every key of a dependency table, dotted names flattened (`mcpplibs.os = "…"` is one key, `"omui.lang" = {…}` too).
void keys_of(const toml::table& table, std::string prefix, const std::filesystem::path& from, const std::filesystem::path& root, Package& package) {
    for (const auto& [key, value] : table) {
        const std::string name = prefix.empty() ? std::string(key.str()) : std::format("{}.{}", prefix, key.str());
        if (const auto* inner = value.as_table()) {
            if (const auto path = (*inner)["path"].value<std::string>()) {
                package.dependencies.push_back(name);
                package.path_dependencies.push_back(generic((from / mcpplibs::os::fs::from_utf8(*path)).lexically_normal().lexically_relative(root)));
            } else if ((*inner)["version"] || (*inner)["git"]) {
                package.dependencies.push_back(name);
            } else {
                keys_of(*inner, name, from, root, package);
            }
        } else {
            package.dependencies.push_back(name);
        }
    }
}

Package package_of(const toml::table& manifest, const std::filesystem::path& dir, const std::filesystem::path& root) {
    Package package{.dir = dir, .relative = dir == root ? std::string() : generic(dir.lexically_relative(root)),
                    .other_repository = another_repository(dir, root)};
    const auto space = manifest["package"]["namespace"].value<std::string>();
    const auto name = manifest["package"]["name"].value<std::string>().value_or("");
    package.name = space ? std::format("{}.{}", *space, name) : name;
    if (const auto* metadata = manifest.at_path("package.metadata.openmui").as_table())
        for (const auto& [key, value] : *metadata) {
            if (const auto text = value.value<std::string>()) package.metadata[std::string(key.str())] = *text;
        }
    // Its profile: what it says, or an application's kind (`kind = "app"`, as omui's templates write it).
    if (const auto found = package.metadata.find("profile"); found != package.metadata.end()) package.profile = found->second;
    else if (const auto kind = package.metadata.find("kind"); kind != package.metadata.end() && kind->second == "app") package.profile = "app";
    const auto* targets = manifest["targets"].as_table();
    package.library = !targets || targets->empty();
    if (targets)
        for (const auto& [_, target] : *targets)
            if (const auto* one = target.as_table(); one && (*one)["kind"].value<std::string>() == "lib") package.library = true;
    for (const std::string_view section : {"dependencies", "dev-dependencies", "build-dependencies"})
        if (const auto* table = manifest[section].as_table()) keys_of(*table, "", dir, root, package);
    return package;
}

}  // namespace

std::expected<Project, std::string> read(const std::filesystem::path& given) {
    std::error_code error;
    Project project{.root = std::filesystem::absolute(given, error).lexically_normal()};
    if (project.root.filename().empty()) project.root = project.root.parent_path();  // "dir/" → "dir"
    std::string problem;
    const auto root_manifest = parsed(project.root / "mcpp.toml", problem);
    if (!root_manifest) return std::unexpected(problem.empty() ? std::string("no mcpp.toml") : problem);
    if ((*root_manifest)["package"]) project.packages.push_back(package_of(*root_manifest, project.root, project.root));
    if (const auto* list = root_manifest->at_path("workspace.members").as_array()) {
        project.workspace = true;
        for (const auto& entry : *list) {
            const auto pattern = entry.value<std::string>();
            if (!pattern) continue;
            if (!pattern->ends_with("/*")) {
                project.members.push_back(*pattern);
                continue;
            }
            std::vector<std::string> children;
            for (const auto& child : std::filesystem::directory_iterator(project.root / mcpplibs::os::fs::from_utf8(pattern->substr(0, pattern->size() - 2)), error))
                if (std::filesystem::exists(child.path() / "mcpp.toml", error)) children.push_back(generic(child.path().lexically_relative(project.root)));
            std::ranges::sort(children);
            project.members.insert(project.members.end(), children.begin(), children.end());
        }
    } else if ((*root_manifest)["workspace"]) {
        project.workspace = true;
    }
    for (const auto& member : project.members) {
        const auto dir = project.root / mcpplibs::os::fs::from_utf8(member);
        auto manifest = parsed(dir / "mcpp.toml", problem);
        if (!manifest) {
            project.problems.push_back(problem);
            continue;
        }
        project.packages.push_back(package_of(*manifest, dir, project.root));
    }
    // Each package's own files: under its directory, not in a nested package's or a build tree.
    std::set<std::filesystem::path> package_dirs;
    for (const auto& package : project.packages) package_dirs.insert(package.dir);
    for (auto& package : project.packages) {
        for (auto walker = std::filesystem::recursive_directory_iterator(package.dir, std::filesystem::directory_options::skip_permission_denied, error);
             walker != std::filesystem::recursive_directory_iterator(); walker.increment(error)) {
            if (error) break;
            const auto leaf = mcpplibs::os::fs::utf8(walker->path().filename());
            if (walker->is_directory(error)) {
                const bool nested = package_dirs.contains(walker->path()) || std::filesystem::exists(walker->path() / "mcpp.toml", error) ||
                                    std::filesystem::exists(walker->path() / ".git", error);
                // A dot-directory is a tool's state (.openmui, .mcpp, .git), not the package's sources.
                if (nested || leaf.starts_with('.') || std::ranges::contains(build_trees, leaf)) walker.disable_recursion_pending();
                continue;
            }
            package.files.push_back(generic(walker->path().lexically_relative(package.dir)));
        }
        std::ranges::sort(package.files);
    }
    return project;
}

}  // namespace mlibs::project
