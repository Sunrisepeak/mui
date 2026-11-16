// The rules over what the sources import (moved from tools/lint/lint.py): imports and module-closure.
//
// imports: one build directory holds every member's modules, so here a source that imports a module no
// dependency of its package gives still builds — and fails where the member is built on its own (CI's rows,
// a consumer outside the tree). A member's sources may import what its dependencies, its target dependencies
// and its features' dependencies reach; its tests, also what its dev-dependencies reach.
//
// module-closure: an interface that `export import`s another package's module gives every consumer that
// package's interface; a library under modules/ lists each one it means in [package.metadata.openmui] reexports.
module omui.devtools.lint;

import std;
import tomlplusplus;
import :tree;

namespace omui::devtools::lint {

namespace fs = std::filesystem;

namespace {

struct Source {
    std::string member, path;  // the deepest member that holds it; its path from the root
    std::string code;          // its text, comments and literals blanked
};

std::string package_name(const toml::table& data) {
    const auto* package = data.get("package") ? data.get("package")->as_table() : nullptr;
    if (!package) return {};
    const std::string name = package->get("name") ? package->get("name")->value_or(std::string{}) : std::string{};
    const auto space = package->get("namespace") ? package->get("namespace")->value<std::string>() : std::nullopt;
    return space ? *space + "." + name : name;
}

const toml::table* table_at(const toml::table* data, std::string_view key) {
    if (!data || !data->get(key)) return nullptr;
    return data->get(key)->as_table();
}

// One fnmatch part: `*` any run, `?` one character.
bool part_matches(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) return text.empty();
    if (pattern.front() == '*') {
        for (std::size_t skip = 0; skip <= text.size(); ++skip)
            if (part_matches(pattern.substr(1), text.substr(skip))) return true;
        return false;
    }
    if (text.empty()) return false;
    return (pattern.front() == '?' || pattern.front() == text.front()) && part_matches(pattern.substr(1), text.substr(1));
}

std::vector<std::string_view> split(std::string_view path) {
    std::vector<std::string_view> parts;
    for (auto part : std::views::split(path, '/')) parts.emplace_back(part.begin(), part.end());
    return parts;
}

// PurePosixPath(path).match(pattern) for a relative pattern: its parts against the path's last parts.
bool path_matches(std::string_view path, std::string_view pattern) {
    const auto have = split(path), want = split(pattern);
    if (want.size() > have.size()) return false;
    const std::size_t offset = have.size() - want.size();
    for (std::size_t at = 0; at < want.size(); ++at)
        if (!part_matches(want[at], have[offset + at])) return false;
    return true;
}

std::vector<std::string> feature_globs(const toml::table& data, std::vector<std::string>* names = nullptr) {
    std::vector<std::string> globs;
    if (const auto* features = table_at(&data, "features"))
        for (const auto& [name, spec] : *features)
            if (const auto* table = spec.as_table())
                if (const auto* list = table->get("sources") ? table->get("sources")->as_array() : nullptr)
                    for (const auto& item : *list)
                        if (auto pattern = item.value<std::string>()) {
                            globs.push_back(*pattern);
                            if (names) names->emplace_back(name.str());
                        }
    return globs;
}

// A line's module name after `prefix` (leading blanks allowed, the name then `;`), or nothing.
std::optional<std::string_view> declared(std::string_view line, std::initializer_list<std::string_view> words) {
    std::size_t at = line.find_first_not_of(" \t");
    if (at == std::string_view::npos) return std::nullopt;
    for (const auto word : words) {
        if (line.substr(at).substr(0, word.size()) != word) return std::nullopt;
        at += word.size();
        const auto after = line.find_first_not_of(" \t", at);
        if (after == at || after == std::string_view::npos) return std::nullopt;  // a blank must follow
        at = after;
    }
    std::size_t end = at;
    while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_' || line[end] == '.')) ++end;
    if (end == at) return std::nullopt;
    const auto rest = line.find_first_not_of(" \t", end);
    if (rest == std::string_view::npos || line[rest] != ';') return std::nullopt;
    return line.substr(at, end - at);
}

template <class Each>
void each_line(std::string_view text, Each&& each) {
    for (auto part : std::views::split(text, '\n')) each(std::string_view(part.begin(), part.end()));
}

std::vector<std::string_view> exported_modules(std::string_view code) {
    std::vector<std::string_view> out;
    each_line(code, [&](std::string_view line) {
        if (auto name = declared(line, {"export", "module"})) out.push_back(*name);
    });
    return out;
}

std::vector<std::string> imported_modules(std::string_view code, bool exported_only) {
    std::set<std::string> out;
    each_line(code, [&](std::string_view line) {
        if (auto name = declared(line, {"export", "import"})) out.emplace(*name);
        else if (!exported_only)
            if (auto plain = declared(line, {"import"})) out.emplace(*plain);
    });
    return {out.begin(), out.end()};
}

// Every .cpp and .cppm, each with the deepest member that holds it.
std::vector<Source> member_sources(const Workspace& workspace) {
    std::vector<std::string> by_depth;
    for (const auto& member : workspace.members)
        if (member.data) by_depth.push_back(member.path);
    std::ranges::sort(by_depth, [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    std::vector<Source> out;
    std::error_code error;
    constexpr std::array skip{"target", ".git", ".cache", "build", "node_modules", "fixtures", ".venv", "__pycache__"};
    for (auto it = fs::recursive_directory_iterator(workspace.root, fs::directory_options::skip_permission_denied, error);
         it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (error) break;
        const std::string name = utf8_of(it->path().filename());
        if (it->is_directory(error)) {
            if (std::ranges::contains(skip, std::string_view(name))) it.disable_recursion_pending();
            continue;
        }
        if (!name.ends_with(".cpp") && !name.ends_with(".cppm")) continue;
        const std::string rel = utf8_of(fs::relative(it->path(), workspace.root));
        for (const auto& member : by_depth) {
            if (!rel.starts_with(member + "/")) continue;
            out.push_back({member, rel, {}});
            break;
        }
    }
    std::ranges::sort(out, {}, &Source::path);
    // Reading and blanking: shared out among the machine's threads.
    std::atomic<std::size_t> next{0};
    {
        std::vector<std::jthread> threads;
        for (std::size_t worker = 0; worker < std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, 32); ++worker)
            threads.emplace_back([&] {
                for (std::size_t index = next++; index < out.size(); index = next++)
                    out[index].code = strip_comments(read_text(workspace.root / path_of(out[index].path)).value_or(""));
            });
    }
    return out;
}

}  // namespace

std::vector<Problem> check_modules(const Workspace& workspace) {
    std::vector<Problem> problems;
    const auto sources = member_sources(workspace);
    std::map<std::string, const toml::table*> manifests;
    for (const auto& member : workspace.members)
        if (member.data) manifests[member.path] = &*member.data;

    // Which package exports each module (the first source, as the script took it).
    std::map<std::string, std::string, std::less<>> owner, interface_owner;
    for (const auto& source : sources) {
        if (source.path.contains("/tests/")) continue;
        const std::string package = package_name(*manifests[source.member]);
        for (const auto module : exported_modules(source.code)) {
            owner.try_emplace(std::string(module), package);
            if (source.path.ends_with(".cppm")) interface_owner.try_emplace(std::string(module), package);
        }
    }

    // imports
    std::map<std::tuple<std::string, bool, std::set<std::string>>, std::set<std::string>> reached;
    const auto reach = [&](const std::string& member, bool tests, const std::set<std::string>& features) {
        const toml::table& data = *manifests[member];
        std::vector<const toml::table*> tables{table_at(&data, "dependencies")};
        if (const auto* targets = table_at(&data, "target"))
            for (const auto& [_, target] : *targets) tables.push_back(table_at(target.as_table(), "dependencies"));
        for (const auto& feature : features_on(data, features, true)) tables.push_back(table_at(table_at(&data, "feature-deps"), feature));
        if (tests) tables.push_back(table_at(&data, "dev-dependencies"));
        std::set<std::string> names{package_name(data)};
        for (const auto* table : tables) {
            if (!table) continue;
            for (const auto& [_, node] : flatten(*table)) {
                const auto* spec = node->as_table();
                const auto path = spec && spec->get("path") ? spec->get("path")->value<std::string>() : std::nullopt;
                if (!path) continue;
                const std::string dependency = resolved(member, *path);
                if (dependency.empty() || !fs::is_regular_file(workspace.root / path_of(dependency) / "mcpp.toml")) continue;
                std::set<std::string> asked;
                if (const auto* list = spec->get("features") ? spec->get("features")->as_array() : nullptr)
                    for (const auto& item : *list)
                        if (auto one = item.value<std::string>()) asked.insert(*one);
                const bool defaults = !spec->get("default-features") || spec->get("default-features")->value_or(true);
                names.merge(closure_of(workspace, dependency, asked, defaults));
            }
        }
        return names;
    };
    for (const auto& source : sources) {
        const bool tests = source.path.starts_with(source.member + "/tests/");
        std::set<std::string> features;
        if (!tests) {  // a package's own tests are built with its defaults
            std::vector<std::string> names;
            const auto globs = feature_globs(*manifests[source.member], &names);
            const std::string within = source.path.substr(source.member.size() + 1);
            for (std::size_t at = 0; at < globs.size(); ++at)
                if (path_matches(within, globs[at])) features.insert(names[at]);
        }
        const auto key = std::tuple{source.member, tests, features};
        auto found = reached.find(key);
        if (found == reached.end()) found = reached.emplace(key, reach(source.member, tests, features)).first;
        for (const auto& module : imported_modules(source.code, false)) {
            const auto package = owner.find(module);
            if (package == owner.end() || found->second.contains(package->second)) continue;
            problems.push_back({"imports", source.path,
                                std::format("imports {} of {}, which {}/mcpp.toml does not reach; declare it in {}", module, package->second, source.member,
                                            tests ? "[dev-dependencies]" : "[dependencies]")});
        }
    }

    // module-closure: the libraries under modules/
    for (const auto& member : workspace.members) {
        if (!member.data || !member.path.starts_with("modules/")) continue;
        const std::string name = package_name(*member.data);
        std::set<std::string> listed;
        if (const auto* list = [&]() -> const toml::array* {
                const auto* metadata = table_at(table_at(table_at(&*member.data, "package"), "metadata"), "openmui");
                return metadata && metadata->get("reexports") ? metadata->get("reexports")->as_array() : nullptr;
            }())
            for (const auto& item : *list)
                if (auto one = item.value<std::string>()) listed.insert(*one);
        const auto globs = feature_globs(*member.data);
        std::map<std::string, std::string> found;  // package → the first interface that re-exports it
        for (const auto& source : sources) {
            if (!source.path.ends_with(".cppm") || source.path.contains("/tests/") || source.member != member.path) continue;
            const std::string within = source.path.substr(member.path.size() + 1);
            if (std::ranges::any_of(globs, [&](const std::string& pattern) { return path_matches(within, pattern); })) continue;  // a feature's
            for (const auto& module : imported_modules(source.code, true)) {
                const auto package = interface_owner.find(module);
                if (package == interface_owner.end() || package->second == name || name.starts_with(package->second + ".")) continue;
                found.try_emplace(package->second, source.path);
            }
        }
        for (const auto& [package, where] : found)
            if (!listed.contains(package))
                problems.push_back({"module-closure", where,
                                    std::format("re-exports {} (every consumer of {} gets its interface); if meant, list it in {}/mcpp.toml "
                                                "[package.metadata.openmui] reexports", package, name, member.path)});
        for (const auto& package : listed)
            if (!found.contains(package))
                problems.push_back({"module-closure", member.path + "/mcpp.toml", std::format("reexports lists {}, which no interface of {} re-exports", package, name)});
    }
    return problems;
}

}  // namespace omui::devtools::lint
