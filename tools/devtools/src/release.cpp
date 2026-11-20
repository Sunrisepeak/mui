module omui.devtools.release;

import std;
import tomlplusplus;
import mcpplibs.os;
import omui.devtools.members;

namespace omui::devtools::release {

namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// "a/b/../c" → "a/c"; a path that climbs above the root keeps its "..".
std::string normal(std::string_view path) {
    std::vector<std::string_view> parts;
    for (const auto part : std::views::split(path, '/')) {
        const std::string_view name(part.begin(), part.end());
        if (name.empty() || name == ".")
            continue;
        if (name == ".." && !parts.empty() && parts.back() != "..")
            parts.pop_back();
        else
            parts.push_back(name);
    }
    std::string joined;
    for (const auto name : parts) {
        if (!joined.empty())
            joined += '/';
        joined += name;
    }
    return joined;
}

const toml::table* table_at(const toml::table* data, std::string_view key) {
    if (!data)
        return nullptr;
    const auto* node = data->get(key);
    return node ? node->as_table() : nullptr;
}

std::string string_at(const toml::table* data, std::string_view key) {
    if (!data || !data->get(key))
        return {};
    return data->get(key)->value_or(std::string{});
}

std::vector<std::string> strings_at(const toml::table* data, std::string_view key) {
    std::vector<std::string> out;
    if (const auto* list = data && data->get(key) ? data->get(key)->as_array() : nullptr)
        for (const auto& item : *list)
            if (auto text = item.value<std::string>())
                out.push_back(*text);
    return out;
}

// One dependency: its name with the tables above it joined (`compat.sdl3`), and its value.
struct Edge {
    std::string name;
    const toml::node* node = nullptr;

    [[nodiscard]] const toml::table* spec() const {
        return node->as_table();
    }
    [[nodiscard]] std::string path() const {
        return string_at(spec(), "path");
    }
    [[nodiscard]] std::string version() const {
        return spec() ? string_at(spec(), "version") : node->value_or(std::string{});
    }
    [[nodiscard]] std::set<std::string> features() const {
        const auto listed = strings_at(spec(), "features");
        return {listed.begin(), listed.end()};
    }
    [[nodiscard]] bool defaults() const {
        return !spec() || !spec()->get("default-features") ||
               spec()->get("default-features")->value_or(true);
    }
};

// A table that is no dependency's own (no path, version, git or workspace key) holds names below
// it.
void flatten(const toml::table& table, const std::string& prefix, std::vector<Edge>& out) {
    for (const auto& [key, node] : table) {
        const std::string name =
            prefix.empty() ? std::string(key.str()) : std::format("{}.{}", prefix, key.str());
        const auto* spec = node.as_table();
        if (!spec || spec->contains("path") || spec->contains("version") || spec->contains("git") ||
            spec->contains("workspace"))
            out.push_back({name, &node});
        else
            flatten(*spec, name, out);
    }
}

// The features on: those asked, the defaults when `defaults`, and what each implies.
std::set<std::string> features_on(const toml::table& data, const std::set<std::string>& asked,
                                  bool defaults) {
    const auto* table = table_at(&data, "features");
    std::vector<std::string> pending(asked.begin(), asked.end());
    if (defaults)
        for (auto& name : strings_at(table, "default"))
            pending.push_back(std::move(name));
    std::set<std::string> active;
    while (!pending.empty()) {
        const std::string feature = std::move(pending.back());
        pending.pop_back();
        if (feature.contains('/') || !active.insert(feature).second)
            continue;
        const toml::node* spec = table ? table->get(feature) : nullptr;
        if (!spec)
            continue;
        if (const auto* list = spec->as_array()) {
            for (const auto& item : *list)
                if (auto text = item.value<std::string>())
                    pending.push_back(*text);
        } else if (const auto* inner = spec->as_table()) {
            for (auto& name : strings_at(inner, "implies"))
                pending.push_back(std::move(name));
        }
    }
    return active;
}

// The dependency tables a consumer's build reads (never the development ones): [dependencies],
// [build-dependencies], each platform's, and the feature-deps of `features` (of every feature when
// null).
std::vector<Edge> edges_of(const toml::table& data, const std::set<std::string>* features,
                           bool development = false) {
    std::vector<const toml::table*> tables{table_at(&data, "dependencies"),
                                           table_at(&data, "build-dependencies")};
    if (development)
        tables.push_back(table_at(&data, "dev-dependencies"));
    if (const auto* targets = table_at(&data, "target"))
        for (const auto& [_, target] : *targets) {
            tables.push_back(table_at(target.as_table(), "dependencies"));
            tables.push_back(table_at(target.as_table(), "build-dependencies"));
            if (development)
                tables.push_back(table_at(target.as_table(), "dev-dependencies"));
        }
    if (const auto* feature_deps = table_at(&data, "feature-deps"))
        for (const auto& [feature, table] : *feature_deps)
            if (!features || features->contains(std::string(feature.str())))
                tables.push_back(table.as_table());
    std::vector<Edge> edges;
    for (const auto* table : tables)
        if (table)
            flatten(*table, {}, edges);
    return edges;
}

struct Manifest {
    std::string path;
    std::string text;
    std::optional<toml::table> data;
    std::string name;
};

std::vector<std::string> listed_members(const fs::path& root) {
    std::vector<std::string> all;
    for (auto& path : members::listed(read(root / "mcpp.toml"))) {
        if (!path.ends_with("/*")) {
            all.push_back(std::move(path));
            continue;
        }
        std::error_code error;
        std::vector<std::string> children;
        for (const auto& child : fs::directory_iterator(root / mcpplibs::os::fs::from_utf8(
                                                                   path.substr(0, path.size() - 2)),
                                                        error))
            if (fs::exists(child.path() / "mcpp.toml", error))
                children.push_back(
                    mcpplibs::os::fs::generic_utf8(child.path().lexically_relative(root)));
        std::ranges::sort(children);
        all.insert(all.end(), children.begin(), children.end());
    }
    return all;
}

// Quoted strings that leave a member's directory, outside its dependency lines ("include_dirs =
// [\"../x\"]").
void collect_outside(std::string_view file, std::string_view text, std::vector<std::string>& out) {
    for (const auto part : std::views::split(text, '\n')) {
        const std::string_view line(part.begin(), part.end());
        const auto code = line.substr(0, line.find('#'));
        if (code.contains("path") && code.contains('{'))
            continue;  // a dependency, counted apart
        for (std::size_t at = code.find('"'); at != std::string_view::npos;
             at = code.find('"', at + 1)) {
            const auto end = code.find('"', at + 1);
            if (end == std::string_view::npos)
                break;
            const std::string_view value = code.substr(at + 1, end - at - 1);
            if (value.starts_with("../")) {
                const std::string seen = std::format("{}: {}", file, value);
                if (!std::ranges::contains(out, seen))
                    out.push_back(seen);
            }
            at = end;
        }
    }
}

}  // namespace

std::expected<Closure, std::string> closure(const fs::path& root) {
    std::vector<Manifest> manifests;
    std::map<std::string, std::size_t> by_path, by_name;
    for (const auto& path : listed_members(root)) {
        Manifest manifest{.path = path,
                          .text = read(root / mcpplibs::os::fs::from_utf8(path) / "mcpp.toml")};
        try {
            manifest.data = toml::parse(manifest.text);
            const auto* package = table_at(&*manifest.data, "package");
            const std::string name = string_at(package, "name"),
                              space = string_at(package, "namespace");
            manifest.name = space.empty() ? name : std::format("{}.{}", space, name);
        } catch (const toml::parse_error&) {}
        by_path[path] = manifests.size();
        if (!manifest.name.empty())
            by_name[manifest.name] = manifests.size();
        manifests.push_back(std::move(manifest));
    }
    if (manifests.empty())
        return std::unexpected("the root manifest lists no members");
    const auto sdk = by_name.find(std::string(template_package));
    if (sdk == by_name.end())
        return std::unexpected(std::format("{} is no member", template_package));
    const std::string sdk_version =
        string_at(table_at(&*manifests[sdk->second].data, "package"), "version");

    Closure out;
    std::set<std::string> external;
    // A state of the walk: a member, the features asked of it, and whether its defaults are on.
    using State = std::tuple<std::size_t, std::set<std::string>, bool>;
    std::vector<State> pending;

    // The roots: what the templates' projects name, rendered as `mcpp new` renders them.
    std::error_code error;
    std::vector<fs::path> templates;
    for (const auto& entry :
         fs::directory_iterator(root / mcpplibs::os::fs::from_utf8(templates_directory), error))
        if (fs::exists(entry.path() / "mcpp.toml.in", error))
            templates.push_back(entry.path() / "mcpp.toml.in");
    std::ranges::sort(templates);
    if (templates.empty())
        return std::unexpected(std::format("no template under {}", templates_directory));
    for (const auto& file : templates) {
        std::string text = read(file);
        for (const auto& [placeholder, value] : std::initializer_list<
                 std::pair<std::string_view, std::string_view>>{{"{{self.name}}", template_package},
                                                                {"{{self.version}}", sdk_version},
                                                                {"{{project.name}}", "project"}})
            for (auto at = text.find(placeholder); at != std::string::npos;
                 at = text.find(placeholder, at + value.size()))
                text.replace(at, placeholder.size(), value);
        const std::string where = mcpplibs::os::fs::generic_utf8(file.lexically_relative(root));
        toml::table data;
        try {
            data = toml::parse(text);
        } catch (const toml::parse_error& failure) {
            out.problems.push_back(std::format("{}: not TOML: {}", where, failure.description()));
            continue;
        }
        for (const auto& edge : edges_of(data, nullptr, true)) {
            if (const auto found = by_name.find(edge.name); found != by_name.end()) {
                if (!std::ranges::contains(out.roots, edge.name))
                    out.roots.push_back(edge.name);
                pending.emplace_back(found->second, edge.features(), edge.defaults());
            } else {
                external.insert(std::format("{}@{}", edge.name, edge.version()));
            }
        }
    }

    // What a template's project builds: the edges its features take.
    std::set<std::size_t> built;
    std::set<State> seen;
    const auto target_of = [&](std::size_t from, const Edge& edge) -> std::optional<std::size_t> {
        const auto path = edge.path();
        if (path.empty())
            return std::nullopt;
        const auto found = by_path.find(normal(std::format("{}/{}", manifests[from].path, path)));
        if (found == by_path.end())
            return std::nullopt;
        return found->second;
    };
    for (std::vector<State> walk = pending; !walk.empty();) {
        auto state = std::move(walk.back());
        walk.pop_back();
        if (!seen.insert(state).second)
            continue;
        const auto& [index, asked, defaults] = state;
        built.insert(index);
        const auto& manifest = manifests[index];
        if (!manifest.data)
            continue;
        const auto features = features_on(*manifest.data, asked, defaults);
        for (const auto& edge : edges_of(*manifest.data, &features))
            if (const auto target = target_of(index, edge))
                walk.emplace_back(*target, edge.features(), edge.defaults());
    }

    // What publishing needs: every edge but the development ones, every feature's.
    std::set<std::size_t> chosen;
    std::vector<std::size_t> walk;
    for (const auto& state : pending)
        walk.push_back(std::get<0>(state));
    std::map<std::size_t, Package> packages;
    while (!walk.empty()) {
        const std::size_t index = walk.back();
        walk.pop_back();
        if (!chosen.insert(index).second)
            continue;
        const auto& manifest = manifests[index];
        if (!manifest.data) {
            out.problems.push_back(std::format("{}: its manifest is not TOML", manifest.path));
            continue;
        }
        const auto* package = table_at(&*manifest.data, "package");
        Package made{.name = manifest.name,
                     .path = manifest.path,
                     .version = string_at(package, "version"),
                     .maturity = string_at(table_at(table_at(table_at(&*manifest.data, "package"),
                                                             "metadata"),
                                                    "openmui"),
                                           "maturity"),
                     .description = string_at(package, "description"),
                     .license = string_at(package, "license"),
                     .repo = string_at(package, "repo"),
                     .built = built.contains(index)};
        // What mcpp publish names the archive's place by: a member without it is refused there.
        if (!made.repo.starts_with("https://"))
            out.problems.push_back(std::format("{}: [package] repo is not an https URL (mcpp publish names the archive's URL by it)", manifest.path));
        for (const auto& edge : edges_of(*manifest.data, nullptr)) {
            const auto path = edge.path();
            if (path.empty()) {
                if (!edge.version().empty()) {
                    const std::string named = std::format("{}@{}", edge.name, edge.version());
                    external.insert(named);
                    if (!std::ranges::contains(made.external, named))
                        made.external.push_back(named);
                }
                continue;
            }
            const auto target = target_of(index, edge);
            if (!target) {
                out.problems.push_back(
                    std::format("{}: `{}` reaches {} by path, which is no member", manifest.path,
                                edge.name, path));
                continue;
            }
            walk.push_back(*target);
        }
        collect_outside("mcpp.toml", manifest.text, made.outside);
        const auto build_program = root / mcpplibs::os::fs::from_utf8(manifest.path) / "build.mcpp";
        if (fs::exists(build_program, error))
            collect_outside("build.mcpp", read(build_program), made.outside);
        packages.emplace(index, std::move(made));
    }
    for (auto& [_, package] : packages)
        out.packages.push_back(std::move(package));
    out.external.assign(external.begin(), external.end());
    return out;
}

std::string descriptor_path(const Package& package) {
    return std::format("pkgs/{}/{}.lua", package.name.front(), package.name);
}

std::vector<std::string> missing_from(const fs::path& index,
                                      std::span<const std::string> external) {
    // Each descriptor's identity (its package table's namespace and name) and its text, for the
    // versions it lists.
    std::map<std::string, std::string> carried;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(index / "pkgs", error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (it->path().extension() != ".lua")
            continue;
        const std::string text = read(it->path());
        std::string space, name;
        for (const auto part : std::views::split(std::string_view(text), '\n')) {
            const std::string_view line(part.begin(), part.end());
            const auto field = [&](std::string_view key) -> std::optional<std::string> {
                if (!line.starts_with("    ") || line.starts_with("     "))
                    return std::nullopt;
                auto rest = line.substr(4);
                if (!rest.starts_with(key))
                    return std::nullopt;
                rest.remove_prefix(key.size());
                const auto open = rest.find('"');
                const auto close = open == std::string_view::npos ? open : rest.find('"', open + 1);
                if (close == std::string_view::npos ||
                    rest.substr(0, open).find_first_not_of(" =") != std::string_view::npos)
                    return std::nullopt;
                return std::string(rest.substr(open + 1, close - open - 1));
            };
            if (auto value = field("namespace"); value && space.empty())
                space = *value;
            if (auto value = field("name"); value && name.empty())
                name = *value;
        }
        if (name.empty())
            continue;
        if (space.empty())
            space = "mcpplibs";
        if (name.starts_with(space + "."))
            name = name.substr(space.size() + 1);  // the compatibility shape
        carried[std::format("{}.{}", space, name)] = text;
    }
    std::vector<std::string> missing;
    for (const auto& named : external) {
        const auto at = named.rfind('@');
        const std::string name = named.substr(0, at);
        std::string version = named.substr(at + 1);
        while (!version.empty() &&
               (version.front() == '^' || version.front() == '~' || version.front() == '='))
            version.erase(0, 1);
        const std::string identity = name.contains('.') ? name : std::format("mcpplibs.{}", name);
        const auto found = carried.find(identity);
        if (found == carried.end() || (!found->second.contains(std::format("[\"{}\"]", version)) &&
                                       !found->second.contains(std::format("['{}']", version))))
            missing.push_back(named);
    }
    return missing;
}

std::map<std::string, std::string> member_paths(const fs::path& root) {
    std::map<std::string, std::string> paths;
    for (const auto& path : listed_members(root)) {
        try {
            const auto data =
                toml::parse(read(root / mcpplibs::os::fs::from_utf8(path) / "mcpp.toml"));
            const auto* package = table_at(&data, "package");
            const std::string name = string_at(package, "name"),
                              space = string_at(package, "namespace");
            if (!name.empty())
                paths[space.empty() ? name : std::format("{}.{}", space, name)] = path;
        } catch (const toml::parse_error&) {}
    }
    return paths;
}

std::string pointed_at(std::string_view manifest, const std::map<std::string, std::string>& members,
                       std::string_view checkout) {
    std::string out;
    bool dependencies = false;
    for (const auto part : std::views::split(manifest, '\n')) {
        std::string line(part.begin(), part.end());
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] == '[') {
            const auto close = line.find(']', first);
            dependencies =
                close != std::string::npos &&
                std::string_view(line).substr(first, close - first).ends_with("dependencies");
        } else if (const auto equals = line.find('=');
                   dependencies && equals != std::string::npos && line.find('#') > equals) {
            std::string name = line.substr(0, equals);
            std::erase_if(name, [](char character) {
                return character == '"' || character == ' ' || character == '\t';
            });
            const auto member = members.find(name);
            const auto value = line.find_first_not_of(" \t", equals + 1);
            if (member != members.end() && value != std::string::npos) {
                const std::string path = std::format("path = \"{}/{}\"", checkout, member->second);
                if (line[value] == '"') {
                    const auto end = line.find('"', value + 1);
                    if (end != std::string::npos)
                        line.replace(value, end + 1 - value, std::format("{{ {} }}", path));
                } else if (line[value] == '{') {
                    const auto key = line.find("version", value);
                    const auto open = key == std::string::npos ? key : line.find('"', key);
                    const auto end = open == std::string::npos ? open : line.find('"', open + 1);
                    if (end != std::string::npos)
                        line.replace(key, end + 1 - key, path);
                }
            }
        }
        out += line;
        out += '\n';
    }
    if (!manifest.ends_with('\n') && !out.empty())
        out.pop_back();
    return out;
}


std::string as_own_manifest(std::string_view descriptor) {
    // The field is `    mcpp = {` … `    },` at the package table's first level (mcpp emit xpkg's layout).
    const std::string_view opening = "\n    mcpp = {";
    const auto start = descriptor.find(opening);
    if (start == std::string_view::npos) return std::string(descriptor);
    const std::string_view closing = "\n    },";
    const auto end = descriptor.find(closing, start + opening.size());
    if (end == std::string_view::npos) return std::string(descriptor);
    std::string out(descriptor.substr(0, start));
    out += "\n    -- The package's own manifest (release try: mcpp reads publish's inline field as a recipe; mcpp#960).";
    out += "\n    mcpp = \"*/mcpp.toml\",";
    out += descriptor.substr(end + closing.size());
    return out;
}

std::pair<std::string, std::string> identity_of(std::string_view descriptor) {
    const auto field = [&](std::string_view key) {
        // `    key = "value",` at the package table's first level.
        const std::string marker = std::format("\n    {} = \"", key);
        const auto at = descriptor.find(marker);
        if (at == std::string_view::npos) return std::string();
        const auto from = at + marker.size();
        const auto to = descriptor.find('"', from);
        return to == std::string_view::npos ? std::string() : std::string(descriptor.substr(from, to - from));
    };
    return {field("namespace"), field("name")};
}

fs::path store_path(std::string_view namespace_, std::string_view name, std::string_view version) {
    // A package without a namespace is resolved in mcpp's default namespace, `mcpplibs` (a bare name
    // is the deprecated fallback, mcpp 2026.11.18.1 warns), and its payload looked for there.
    if (namespace_.empty()) return fs::path("xpkgs") / std::format("mcpplibs-x-{}", name) / std::string(version);
    return fs::path("xpkgs") / std::format("{}-x-{}", namespace_, name) / std::string(version);
}

std::string with_default_index(std::string_view config, std::string_view url) {
    const std::string_view header = "[index.repos.\"mcpplibs\"]";
    const auto at = config.find(header);
    if (at == std::string_view::npos) return {};
    // The table ends at the next table's header.
    auto end = config.find("\n[", at + header.size());
    if (end == std::string_view::npos) end = config.size();
    std::string out(config.substr(0, at));
    out += std::format("{}\nurl    = \"{}\"\nsource = \"git\"\n", header, url);
    out += config.substr(end);
    return out;
}

std::string toml_problem(std::string_view manifest) {
    try {
        static_cast<void>(toml::parse(manifest));
        return {};
    } catch (const toml::parse_error& failure) {
        return std::format("{}:{}: {}", failure.source().begin.line, failure.source().begin.column, failure.description());
    }
}

std::expected<std::string, std::string> pinned(std::string_view manifest, std::string_view directory,
                                               const std::map<std::string, std::string>& versions) {
    toml::table data;
    try {
        data = toml::parse(manifest);
    } catch (const toml::parse_error& failure) {
        return std::unexpected(std::format("not TOML: {}", failure.description()));
    }
    int changed = 0;
    std::string missing;
    // A dependency table (or a group of them by namespace): every entry with a `path` becomes its member's version.
    const std::function<void(toml::table&)> pin = [&](toml::table& table) {
        std::vector<std::pair<std::string, std::string>> bare;  // entries that become a version alone, set after the walk
        for (auto&& [key, node] : table) {
            toml::table* entry = node.as_table();
            if (!entry) continue;
            const auto* path = entry->get_as<std::string>("path");
            if (!path) {
                pin(*entry);  // a namespace's members: `mlibs = { agent = { path = … } }`
                continue;
            }
            const std::string target = normal(std::format("{}/{}", directory, path->get()));
            const auto version = versions.find(target);
            if (version == versions.end()) {
                missing += std::format(" {}", target);
                continue;
            }
            entry->erase("path");
            if (entry->empty()) bare.emplace_back(std::string(key.str()), version->second);
            else entry->insert_or_assign("version", version->second);
            ++changed;
        }
        for (const auto& [key, version] : bare) table.insert_or_assign(key, version);
    };
    for (const std::string_view name : {"dependencies", "build-dependencies"})
        if (auto* table = data.get_as<toml::table>(name)) pin(*table);
    if (auto* features = data.get_as<toml::table>("feature-deps"))
        for (auto&& [feature, node] : *features)
            if (auto* table = node.as_table()) pin(*table);
    if (!missing.empty()) return std::unexpected(std::format("a path that is no member:{}", missing));
    if (changed == 0) return std::string(manifest);
    std::ostringstream out;
    out << "# Release try: dependencies on members pinned to their versions (mcpp#962).\n" << data << '\n';
    return out.str();
}

}  // namespace omui::devtools::release
