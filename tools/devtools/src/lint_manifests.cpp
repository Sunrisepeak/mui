// The rules over the workspace's manifests (the second group moved from tools/lint/lint.py): toml,
// members, manifest, layering, modules, determinism's flags, project-dag and layout.
module omui.devtools.lint;

import std;
import tomlplusplus;
import :tree;

namespace omui::devtools::lint {

namespace fs = std::filesystem;

namespace {

constexpr std::array skip_dirs{"target", ".git", ".cache", "build", "node_modules", "fixtures", ".venv", "__pycache__"};

// Layers (D-80, D-82; convergence plan §9.2). A name says who owns a package, a directory which group it
// is in, and the layer comes from this table: the longest path prefix that holds a member gives its layer,
// so one project may span layers (hanim.core is base, hanim is gfx). A layer may depend on the layers
// listed for it (path dependencies in [dependencies] and target dependencies).
//
// The independent components (mlibs, hanim, xrender, xengine's kernels below the UI) know nothing of omui
// (D-55); omui never depends on the game layer (xengine/game, D-161); gongfu's core depends on the standard
// library alone, so that it can leave the repository unchanged, and the rest of gongfu on no UI.
constexpr std::pair<std::string_view, std::string_view> path_layers[]{
    // mlibs: the layer groups
    {"modules/base/utils", "utils"},  // the bottom: small tools, the standard library alone
    {"modules/base", "base"},
    {"modules/gfx", "gfx"},
    {"modules/content", "content"},
    {"modules/media", "media"},
    {"modules/base/mpal", "base"},  // the OS's services above mcpplibs.os: no UI, used by omui's hosts
    // the projects (plan §9.2)
    {"modules/projects/hanim/core", "base"},
    {"modules/projects/hanim/timeline", "base"},  // tracks of clips, their file and edits: hanim.core and mlibs alone
    {"modules/projects/hanim", "gfx"},
    {"modules/projects/xrender", "gfx"},
    {"modules/projects/xrender/text", "content"},  // text as the renderer draws it: above mlibs.text
    {"modules/projects/xengine/kernel/dynamics", "base"},  // the CPU core (hanim and omui use it, D-133)
    {"modules/projects/xengine/kernel/cellular", "base"},  // its CPU part
    {"modules/projects/xengine/kernel/cellular/rhi", "gfx"},
    {"modules/projects/xengine/kernel/cellular/present", "gfx"},  // cells to an xrender image
    {"modules/projects/xengine/kernel/audio", "media"},
    {"modules/projects/xengine/kernel/video", "media"},
    {"modules/projects/xengine/kernel/matter", "base"},     // materials, their states and laws (D-165)
    {"modules/projects/xengine/kernel/particles", "base"},  // things that fly in 3D, a carrier of matter (D-166)
    {"modules/projects/xengine/kernel/field", "base"},      // the air and its wind, a carrier of matter (M6.5 T5.2)
    {"modules/projects/xengine/kernel/matter/lab", "gfx"},  // the material lab: the lattice's presenter and the renderer's frame
    {"modules/projects/xengine/game", "game"},              // above the interface: world, input, net, ui (the bridge), runtime
    {"modules/projects/gongfu", "gongfu"},
    {"modules/projects/omui", "ui"},
    {"modules/projects/omui/host", "host"},
    {"modules/projects/omui/components", "components"},
    {"modules/projects/omui/lang", "lang"},
    {"modules/projects/omui/sdk", "sdk"},
    {"modules/projects/omui/tools", "tools"},  // omui's toolkit: the command line, MCP, the generator (plan §4.5)
    {"modules/ui", "ui"},
    {"components", "components"},
    {"lang", "lang"},
    {"mgame", "game"},
    {"spec", "spec"},
    // build code, tools and the consumers
    {"tools", "tools"},
    {"mcpp", "mcpp"},
    {"modules/projects/omui/entry", "app"},  // omui's programs: the composition roots
    {"tests", "app"},
    {"examples", "app"},
    {"showcase", "app"},
    {"site", "app"},
    {".", "app"},
};
// A directory that is its own layer, not the prefix of one: gongfu's core.
constexpr std::pair<std::string_view, std::string_view> exact_layers[]{{"modules/projects/gongfu", "gongfu.core"}};
constexpr std::array independent{"utils", "base", "gfx", "content", "media"};

struct Allowed {
    std::string_view layer;
    std::vector<std::string_view> may;  // "*": any
};

const std::vector<Allowed>& layer_rules() {
    static const std::vector<Allowed> table = [] {
        const std::vector<std::string_view> below_ui{"utils", "base", "gfx", "content", "media", "spec"};
        const auto with = [&](std::vector<std::string_view> more) {
            auto all = below_ui;
            all.insert(all.end(), more.begin(), more.end());
            return all;
        };
        return std::vector<Allowed>{
            {"utils", {}},
            {"base", {"utils", "base"}},
            {"gfx", {"utils", "base", "gfx"}},
            {"content", {"utils", "base", "gfx", "content"}},
            {"media", {"utils", "base", "gfx", "content", "media"}},
            {"gongfu.core", {}},
            {"gongfu", {"utils", "base", "gfx", "gongfu.core", "gongfu"}},
            {"spec", {}},
            {"ui", with({"ui"})},
            {"host", with({"ui", "host"})},
            {"components", with({"ui", "components"})},
            {"lang", {"utils", "spec", "ui", "components", "lang"}},  // utils: mlibs.error, the error contract every layer answers in
            {"sdk", with({"ui", "host", "components", "sdk"})},
            {"game", with({"ui", "host", "components", "game", "gongfu.core", "gongfu"})},
            {"tools", {"*"}},
            {"mcpp", {"tools", "mcpp"}},  // build code only: the plugins, their shared base and the generator they run
            {"app", {"*"}},
        };
    }();
    return table;
}

// Projects (D-161): the units that may each be a repository of their own one day, and the projects each
// may depend on. Test dependencies count: a project whose tests need a project above it could not be
// built alone. The longest path prefix that holds a member names its project; consumers (tools, the
// site, showcases, examples, tests, entries) are free.
constexpr std::pair<std::string_view, std::string_view> project_paths[]{
    {"modules", "mlibs"},
    {"spec", "spec"},
    {"modules/projects/xrender", "xrender"},
    {"modules/projects/xengine/kernel/dynamics", "xengine.sim"},
    {"modules/projects/xengine/kernel/cellular", "xengine.sim"},
    {"modules/projects/xengine/kernel/matter", "xengine.sim"},
    {"modules/projects/xengine/kernel/particles", "xengine.sim"},
    {"modules/projects/xengine/kernel/field", "xengine.sim"},
    {"modules/projects/hanim", "hanim"},
    {"modules/projects/xengine/kernel/audio", "xengine.media"},
    {"modules/projects/xengine/kernel/video", "xengine.media"},
    {"modules/projects/omui", "omui"},
    {"modules/projects/xengine/game", "xengine.game"},
    {"modules/projects/gongfu", "gongfu"},
};
// gongfu's core alone (the standard library): a fixture the game layer's tests may take.
constexpr std::pair<std::string_view, std::string_view> exact_projects[]{{"modules/projects/gongfu", "gongfu.core"}};

const std::map<std::string_view, std::set<std::string_view>>& project_rules() {
    static const std::map<std::string_view, std::set<std::string_view>> table = [] {
        const std::set<std::string_view> kernels{"mlibs", "xrender", "xengine.sim", "hanim", "xengine.media"};
        const auto with = [&](std::initializer_list<std::string_view> more) {
            auto all = kernels;
            all.insert(more);
            return all;
        };
        return std::map<std::string_view, std::set<std::string_view>>{
            {"mlibs", {}},
            {"spec", {}},
            {"xrender", {"mlibs"}},
            {"xengine.sim", {"mlibs", "xrender"}},
            {"hanim", {"mlibs", "xrender", "xengine.sim"}},
            {"xengine.media", {"mlibs", "xrender", "xengine.sim", "hanim"}},
            {"omui", with({"spec"})},
            {"gongfu.core", {"mlibs"}},  // for its tests; what it is built from is the standard library alone (layering)
            {"gongfu", with({"gongfu.core"})},
            {"xengine.game", with({"spec", "omui", "gongfu.core"})},
        };
    }();
    return table;
}
// In the game layer, the one package that knows omui (D-162).
constexpr std::string_view omui_bridge = "modules/projects/xengine/game/ui";

// Where the path-is-the-name rule (D-80) holds, and the directories whose README lists the packages under them.
constexpr std::array library_roots{"modules", "mgame", "components", "lang", "tools", "showcase", "mcpp"};
constexpr std::array readme_roots{"modules", "mgame", "components", "lang", "tools", "tests", "examples", "showcase", "mcpp"};

// The longest prefix of `member` in a table (".": the root, the shortest of all).
template <std::size_t N>
std::optional<std::string_view> longest(std::string_view member, const std::pair<std::string_view, std::string_view> (&table)[N]) {
    std::optional<std::string_view> best, value;
    for (const auto& [prefix, named] : table) {
        if (!(prefix == "." || member == prefix || (member.starts_with(prefix) && member.substr(prefix.size()).starts_with('/')))) continue;
        if (!best || *best == "." || prefix.size() > best->size()) {
            best = prefix;
            value = named;
        }
    }
    return value;
}

std::optional<std::string_view> layer_of(std::string_view member) {
    for (const auto& [path, layer] : exact_layers)
        if (member == path) return layer;
    return longest(member, path_layers);
}

const Allowed* rule_for(std::string_view member) {
    const auto layer = layer_of(member);
    if (!layer) return nullptr;
    const auto& table = layer_rules();
    const auto found = std::ranges::find(table, *layer, &Allowed::layer);
    return found == table.end() ? nullptr : &*found;
}

std::optional<std::string_view> project_of(std::string_view member) {
    for (const auto& [path, project] : exact_projects)
        if (member == path) return project;
    std::optional<std::string_view> best, value;
    for (const auto& [prefix, project] : project_paths) {
        if (!(member == prefix || (member.starts_with(prefix) && member.substr(prefix.size()).starts_with('/')))) continue;
        if (!best || prefix.size() > best->size()) {
            best = prefix;
            value = project;
        }
    }
    return value;
}

std::string list_of(const auto& names) {
    std::vector<std::string> sorted(names.begin(), names.end());
    std::ranges::sort(sorted);
    std::string text = "[";
    for (const auto& name : sorted) text += std::format("{}'{}'", text.size() > 1 ? ", " : "", name);
    return text + "]";
}

const toml::table* table_at(const toml::table& data, std::string_view key) {
    const auto* node = data.get(key);
    return node ? node->as_table() : nullptr;
}

std::string package_name(const toml::table& data) {
    const auto* package = table_at(data, "package");
    if (!package) return {};
    const std::string name = package->get("name") ? package->get("name")->value_or(std::string{}) : std::string{};
    const auto* space = package->get("namespace");
    return space && space->value<std::string>() ? *space->value<std::string>() + "." + name : name;
}

bool has_skipped_part(std::string_view path) {
    for (auto part : std::views::split(path, '/'))
        if (std::ranges::contains(skip_dirs, std::string_view(part.begin(), part.end()))) return true;
    return false;
}

}  // namespace

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

fs::path path_of(std::string_view utf8) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size())); }

std::string utf8_of(const fs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

Workspace Workspace::read(const fs::path& root) {
    Workspace workspace{.root = root};
    const auto parse = [](const std::string& text, std::string& error) -> std::optional<toml::table> {
        try {
            return toml::parse(text);
        } catch (const toml::parse_error& failure) {
            error = std::format("{}:{}", failure.source().begin.line, std::string(failure.description()));
            return std::nullopt;
        }
    };
    const auto text = read_text(root / "mcpp.toml");
    if (!text) return workspace;
    workspace.root_data = parse(*text, workspace.root_error);
    if (!workspace.root_data) return workspace;
    const auto* space = table_at(*workspace.root_data, "workspace");
    const auto* listed = space && space->get("members") ? space->get("members")->as_array() : nullptr;
    if (!listed) return workspace;
    for (const auto& entry : *listed) {
        const auto pattern = entry.value<std::string>();
        if (!pattern) continue;
        std::vector<std::string> paths;
        if (pattern->ends_with("/*")) {  // a directory's children that hold a manifest
            std::error_code error;
            const fs::path parent = root / path_of(pattern->substr(0, pattern->size() - 2));
            for (const auto& child : fs::directory_iterator(parent, error))
                if (fs::exists(child.path() / "mcpp.toml")) paths.push_back(utf8_of(fs::relative(child.path(), root)));
            std::ranges::sort(paths);
        } else {
            paths.push_back(*pattern);
        }
        for (auto& path : paths) {
            Member member{.path = path};
            if (auto manifest = read_text(root / path_of(path) / "mcpp.toml")) {
                member.text = std::move(*manifest);
                member.data = parse(member.text, member.error);
            }
            workspace.members.push_back(std::move(member));
        }
    }
    return workspace;
}

const Member* Workspace::find(std::string_view path) const {
    const auto found = std::ranges::find(members, path, &Member::path);
    return found == members.end() ? nullptr : &*found;
}

std::vector<std::pair<std::string, const toml::node*>> flatten(const toml::table& table, std::string prefix) {
    std::vector<std::pair<std::string, const toml::node*>> out;
    for (const auto& [key, value] : table) {
        const std::string name = prefix + std::string(key.str());
        const auto* inner = value.as_table();
        if (inner && !inner->contains("path") && !inner->contains("git") && !inner->contains("version") && !inner->contains("workspace")) {
            std::ranges::move(flatten(*inner, name + "."), std::back_inserter(out));
            continue;
        }
        out.emplace_back(name, &value);
    }
    return out;
}

std::string resolved(std::string_view member, std::string_view relative) {
    const fs::path joined = (path_of(member == "." ? "" : member) / path_of(relative)).lexically_normal();
    std::string text = utf8_of(joined);
    while (text.ends_with('/')) text.pop_back();
    if (text.empty() || text == ".") return ".";
    if (text.starts_with("..")) return {};
    return text;
}

std::vector<Problem> check_manifests(const Workspace& workspace) {
    std::vector<Problem> problems;
    const auto add = [&](std::string_view rule, std::string where, std::string message) {
        problems.push_back({std::string(rule), std::move(where), std::move(message)});
    };
    if (!workspace.root_error.empty()) {
        add("toml", "mcpp.toml", std::format("is not TOML ({}). Where a package and one below it are both dependencies (`a.b` and `a.b.c`), "
                                             "the child's key is quoted: `\"a.b.c\" = …` (AGENTS §5)", workspace.root_error));
        return problems;
    }
    std::set<std::string> listed;
    for (const auto& member : workspace.members) listed.insert(member.path);

    // Every manifest under the tree is a listed member (the templates' are not packages).
    std::error_code error;
    std::vector<std::string> manifests;
    for (auto it = fs::recursive_directory_iterator(workspace.root, fs::directory_options::skip_permission_denied, error);
         it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (error) break;
        const std::string name = utf8_of(it->path().filename());
        if (it->is_directory(error)) {
            if (std::ranges::contains(skip_dirs, std::string_view(name))) it.disable_recursion_pending();
            continue;
        }
        if (name != "mcpp.toml") continue;
        const std::string directory = utf8_of(fs::relative(it->path().parent_path(), workspace.root));
        if (directory == "." || listed.contains(directory) || directory.starts_with("modules/projects/omui/templates/") || has_skipped_part(directory))
            continue;
        manifests.push_back(directory);
    }
    std::ranges::sort(manifests);
    for (const auto& directory : manifests) add("members", directory + "/mcpp.toml", "a package that is not a workspace member");

    for (const auto& member : workspace.members) {
        if (member.text.empty() && !fs::exists(workspace.root / path_of(member.path) / "mcpp.toml")) {
            add("members", member.path, "listed as a member, but has no mcpp.toml");
            continue;
        }
        if (!member.data) {
            add("toml", member.path + "/mcpp.toml:" + member.error.substr(0, member.error.find(':')),
                std::format("is not TOML ({}). Where a package and one below it are both dependencies (`a.b` and `a.b.c`), the child's key is "
                            "quoted: `\"a.b.c\" = …` (AGENTS §5)", member.error.substr(member.error.find(':') + 1)));
            continue;
        }
        // Namespaced dependencies are dotted keys of the one table (`compat.sdl3 = "3.4.18"`), never tables of their own.
        for (auto part : std::views::split(std::string_view(member.text), '\n')) {
            std::string_view line(part.begin(), part.end());
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
            for (const std::string_view table : {"dependencies", "dev-dependencies"}) {
                const std::string opening = std::format("[{}.", table);
                if (!line.starts_with(opening) || !line.ends_with(']')) continue;
                const std::string_view name = line.substr(opening.size(), line.size() - opening.size() - 1);
                if (name.empty() || !std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }))
                    continue;
                add("manifest", member.path, std::format("[{}.{}]: write `{}.<name> = ...` under [{}]", table, name, name, table));
            }
        }
        const Allowed* allowed = rule_for(member.path);
        if (!allowed) {
            add("layering", member.path, "a member in no known layer; add it to the lint's layer table (tools/devtools/src/lint_manifests.cpp)");
            continue;
        }
        std::vector<const toml::table*> tables{table_at(*member.data, "dependencies")};
        if (const auto* targets = table_at(*member.data, "target"))
            for (const auto& [_, target] : *targets)
                if (const auto* one = target.as_table()) tables.push_back(table_at(*one, "dependencies"));
        const bool any = std::ranges::contains(allowed->may, std::string_view("*"));
        for (const auto* table : tables) {
            if (!table || any) continue;
            for (const auto& [name, node] : flatten(*table)) {
                const auto* spec = node->as_table();
                const auto path = spec && spec->get("path") ? spec->get("path")->value<std::string>() : std::nullopt;
                if (!path) continue;
                const std::string dependency = resolved(member.path, *path);
                if (dependency.empty()) continue;
                const auto layer = layer_of(dependency);
                if (layer && std::ranges::contains(allowed->may, *layer)) continue;
                const auto own = layer_of(member.path);
                add(own && std::ranges::contains(independent, *own) ? "modules" : "layering", member.path,
                    std::format("depends on {} ({}); allowed layers: {}", dependency, name, allowed->may.empty() ? std::string("std only") : list_of(allowed->may)));
            }
        }
        if (std::ranges::contains(deterministic_members(), std::string_view(member.path))) {
            std::string flags;
            if (const auto* build = table_at(*member.data, "build"))
                if (const auto* list = build->get("cxxflags") ? build->get("cxxflags")->as_array() : nullptr)
                    for (const auto& flag : *list) flags += flag.value_or(std::string{}) + " ";
            if (!flags.contains("-ffp-contract=off")) add("determinism", member.path + "/mcpp.toml", "missing -ffp-contract=off in [build] cxxflags");
        }
    }
    return problems;
}

std::vector<Problem> check_projects(const Workspace& workspace) {
    std::vector<Problem> problems;
    for (const auto& member : workspace.members) {
        const auto own = project_of(member.path);
        if (!own || !member.data) continue;
        std::vector<std::pair<const toml::table*, std::string_view>> tables{{table_at(*member.data, "dependencies"), ""},
                                                                             {table_at(*member.data, "dev-dependencies"), " for its tests"}};
        if (const auto* features = table_at(*member.data, "feature-deps"))
            for (const auto& [_, one] : *features) tables.emplace_back(one.as_table(), "");
        if (const auto* targets = table_at(*member.data, "target"))
            for (const auto& [_, target] : *targets)
                if (const auto* one = target.as_table()) {
                    tables.emplace_back(table_at(*one, "dependencies"), "");
                    tables.emplace_back(table_at(*one, "dev-dependencies"), " for its tests");
                }
        const auto& allowed = project_rules();
        for (const auto& [table, what] : tables) {
            if (!table) continue;
            for (const auto& [name, node] : flatten(*table)) {
                const auto* spec = node->as_table();
                const auto path = spec && spec->get("path") ? spec->get("path")->value<std::string>() : std::nullopt;
                if (!path) continue;
                const std::string dependency = resolved(member.path, *path);
                if (dependency.empty()) continue;
                const auto other = project_of(dependency);
                const auto rule = allowed.find(*own);
                const std::set<std::string_view> may = rule == allowed.end() ? std::set<std::string_view>{} : rule->second;
                if (!other)
                    problems.push_back({"project-dag", member.path, std::format("depends on {} ({}){}: a consumer, which no project depends on", dependency, name, what)});
                else if (*other != *own && !may.contains(*other))
                    problems.push_back({"project-dag", member.path,
                                        std::format("depends on {} ({}){}: {} may depend on {}, not on {}", dependency, name, what, *own,
                                                    may.empty() ? std::string("no project") : list_of(may), *other)});
                else if (*other == "omui" && *own == "xengine.game" && member.path != omui_bridge)
                    problems.push_back({"project-dag", member.path,
                                        std::format("depends on {} ({}){}: in the game layer only {} knows omui (D-162)", dependency, name, what, omui_bridge)});
            }
        }
    }
    return problems;
}

std::vector<Problem> check_layout(const Workspace& workspace) {
    std::vector<Problem> problems;
    std::map<std::string, std::string> names;
    for (const auto& member : workspace.members)
        if (member.data) names[member.path] = package_name(*member.data);
    for (const auto& member : workspace.members) {
        if (!names.contains(member.path)) continue;
        const std::string& full = names[member.path];
        const std::string_view top = std::string_view(member.path).substr(0, member.path.find('/'));
        if (!std::ranges::contains(library_roots, top)) continue;
        const auto slash = member.path.rfind('/');
        const std::string directory = slash == std::string::npos ? member.path : member.path.substr(slash + 1);
        std::string leaf = directory;
        std::ranges::replace(leaf, '-', '_');
        const std::string last = full.substr(full.rfind('.') == std::string::npos ? 0 : full.rfind('.') + 1);
        if (leaf != last)
            problems.push_back({"layout", member.path, std::format("the directory is `{}` but the package is {}: a library's path is its name (D-80)", directory, full)});
        const std::string parent = slash == std::string::npos ? std::string(".") : member.path.substr(0, slash);
        if (const auto found = names.find(parent); found != names.end() && !full.starts_with(found->second + "."))
            problems.push_back({"layout", member.path, std::format("{} lies in {} ({}): a package inside another is named after it ({}.{})", full, parent,
                                                                   found->second, found->second, leaf)});
    }
    // Each directory with members below it lists its children in its README.
    std::map<std::string, std::set<std::string>> children;
    for (const auto& member : workspace.members) {
        std::vector<std::string> parts;
        for (auto part : std::views::split(std::string_view(member.path), '/')) parts.emplace_back(part.begin(), part.end());
        if (parts.empty() || !std::ranges::contains(readme_roots, std::string_view(parts.front()))) continue;
        std::string directory;
        for (std::size_t at = 1; at < parts.size(); ++at) {
            directory += (at > 1 ? "/" : "") + parts[at - 1];
            children[directory].insert(parts[at]);
        }
    }
    for (const auto& [directory, kids] : children) {
        const auto text = read_text(workspace.root / path_of(directory) / "README.md");
        if (!text) {
            problems.push_back({"layout", directory, "a directory with packages under it has a README.md that lists them"});
            continue;
        }
        for (const auto& kid : kids) {
            // a link `](kid)` or `](kid/…)`
            const std::string open = "](" + kid;
            bool linked = false;
            for (auto at = text->find(open); at != std::string::npos && !linked; at = text->find(open, at + 1)) {
                const char next = at + open.size() < text->size() ? (*text)[at + open.size()] : '\0';
                if (next == ')') linked = true;
                else if (next == '/') linked = text->find(')', at + open.size()) != std::string::npos;
            }
            if (!linked) problems.push_back({"layout", directory + "/README.md", std::format("does not list `{}` (a link `]({})`)", kid, kid)});
        }
    }
    return problems;
}

}  // namespace omui::devtools::lint
