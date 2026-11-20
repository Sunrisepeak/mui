// The rules over what the manifests reach (the third group moved from tools/lint/lint.py): licenses,
// platforms, pins and closure.
module omui.devtools.lint;

import std;
import tomlplusplus;
import mcpplibs.os;
import :tree;

namespace omui::devtools::lint {

namespace fs = std::filesystem;

namespace {

const toml::table* table_at(const toml::table* data, std::string_view key) {
    if (!data) return nullptr;
    const auto* node = data->get(key);
    return node ? node->as_table() : nullptr;
}

std::string string_at(const toml::table* data, std::string_view key) {
    if (!data || !data->get(key)) return {};
    return data->get(key)->value_or(std::string{});
}

std::vector<std::string> strings_at(const toml::table* data, std::string_view key) {
    std::vector<std::string> out;
    if (const auto* list = data && data->get(key) ? data->get(key)->as_array() : nullptr)
        for (const auto& item : *list)
            if (auto text = item.value<std::string>()) out.push_back(*text);
    return out;
}

std::string package_name(const toml::table& data) {
    const auto* package = table_at(&data, "package");
    const std::string name = string_at(package, "name"), space = string_at(package, "namespace");
    return space.empty() ? name : space + "." + name;
}

// A dependency's own table: the TOML node as a table, or null for a bare version string.
const toml::table* spec_of(const toml::node* node) { return node ? node->as_table() : nullptr; }

std::optional<std::string> path_in(const toml::node* node) {
    const auto* spec = spec_of(node);
    if (!spec || !spec->get("path")) return std::nullopt;
    return spec->get("path")->value<std::string>();
}

// A dependency's value as the script printed it (a version string, or the table): compared, not shown.
std::string spelled(const toml::node* node) {
    if (!node) return {};
    if (auto text = node->value<std::string>()) return std::format("'{}'", *text);
    std::ostringstream out;
    if (const auto* table = node->as_table()) out << *table;
    return out.str();
}

std::vector<const toml::table*> tables_of(const toml::table& data, std::initializer_list<std::string_view> kinds, bool targets_too = true) {
    std::vector<const toml::table*> out;
    for (const auto kind : kinds) out.push_back(table_at(&data, kind));
    if (targets_too)
        if (const auto* targets = table_at(&data, "target"))
            for (const auto& [_, target] : *targets)
                for (const auto kind : kinds) out.push_back(table_at(target.as_table(), kind));
    return out;
}

// Licences (D-83): GPL and AGPL never, dynamic linking included; LGPL only as an optional plugin linked
// dynamically. tools/lint/licenses.toml lists every index package the workspace reaches.
std::set<std::string> index_packages(const Workspace& workspace) {
    std::set<std::string> names;
    std::vector<const toml::table*> manifests;
    for (const auto& member : workspace.members)
        if (member.data) manifests.push_back(&*member.data);
    if (workspace.root_data) manifests.push_back(&*workspace.root_data);
    for (const auto* data : manifests)
        for (const auto* table : tables_of(*data, {"dependencies", "dev-dependencies", "build-dependencies"}))
            if (table)
                for (const auto& [name, node] : flatten(*table))
                    if (!path_in(node)) names.insert(name);
    if (const auto lock = read_text(workspace.root / "mcpp.lock")) {
        try {
            const auto data = toml::parse(*lock);
            if (const auto* packages = table_at(&data, "package"))
                for (const auto& [key, _] : *packages) names.insert(std::string(key.str()));
        } catch (const toml::parse_error&) {
        }
    }
    return names;
}

// The licences a local mcpp index's descriptor lists, when one is there.
std::optional<std::vector<std::string>> index_licences(std::string_view name) {
    const fs::path indices = path_of(mcpplibs::os::env::get("HOME").value_or("")) / ".mcpp" / "indices";
    std::error_code error;
    std::vector<fs::path> roots;
    for (const auto& index : fs::directory_iterator(indices, error))
        if (fs::is_directory(index.path() / "pkgs")) roots.push_back(index.path() / "pkgs");
    std::ranges::sort(roots);
    for (const auto& pkgs : roots) {
        std::vector<fs::path> candidates;
        const auto add_named = [&](std::string_view file) {
            for (const auto& group : fs::directory_iterator(pkgs, error))
                if (fs::exists(group.path() / path_of(std::string(file) + ".lua"))) candidates.push_back(group.path() / path_of(std::string(file) + ".lua"));
        };
        add_named(name);
        if (name.starts_with("mcpplibs.mwrap.")) candidates.push_back(pkgs / "mwrap" / path_of(std::string(name.substr(15)) + ".lua"));
        if (name.starts_with("mcpplibs.")) add_named(name.substr(9));
        for (const auto& file : candidates) {
            const auto text = read_text(file);
            if (!text) continue;
            static const std::regex licences(R"(licenses\s*=\s*\{([^}]*)\})");
            static const std::regex quoted(R"lit("([^"]+)")lit");
            std::smatch found;
            std::vector<std::string> out;
            if (std::regex_search(*text, found, licences)) {
                const std::string inside = found[1].str();
                for (std::sregex_iterator it(inside.begin(), inside.end(), quoted), end; it != end; ++it) out.push_back((*it)[1].str());
            }
            return out;
        }
    }
    return std::nullopt;
}

std::string python_list(std::vector<std::string> names) {
    std::string text = "[";
    for (const auto& name : names) text += std::format("{}'{}'", text.size() > 1 ? ", " : "", name);
    return text + "]";
}

}  // namespace

std::vector<std::string> features_on(const toml::table& data, const std::set<std::string>& asked, bool defaults) {
    const auto* table = table_at(&data, "features");
    std::vector<std::string> pending(asked.begin(), asked.end());
    if (defaults)
        for (auto& name : strings_at(table, "default")) pending.push_back(std::move(name));
    std::set<std::string> active;
    while (!pending.empty()) {
        const std::string feature = std::move(pending.back());
        pending.pop_back();
        if (active.contains(feature) || feature.contains('/')) continue;
        active.insert(feature);
        const toml::node* spec = table ? table->get(feature) : nullptr;
        if (!spec) continue;
        if (const auto* list = spec->as_array()) {
            for (const auto& item : *list)
                if (auto text = item.value<std::string>()) pending.push_back(*text);
        } else if (const auto* inner = spec->as_table()) {
            for (auto& name : strings_at(inner, "implies")) pending.push_back(std::move(name));
        }
    }
    return {active.begin(), active.end()};
}

std::vector<Problem> check_licences(const Workspace& workspace) {
    std::vector<Problem> problems;
    const auto text = read_text(workspace.root / "tools" / "lint" / "licenses.toml");
    toml::table table;
    if (text) {
        try {
            table = toml::parse(*text);
        } catch (const toml::parse_error& failure) {
            return {{"licenses", "tools/lint/licenses.toml", std::format("not valid TOML: {}", failure.description())}};
        }
    }
    const auto* listed = table_at(&table, "packages");
    const auto* dynamic = table_at(&table, "dynamic");
    for (const auto& name : index_packages(workspace)) {
        const toml::node* entry = listed ? listed->get(name) : nullptr;
        if (!entry) {
            problems.push_back({"licenses", name, "not in tools/lint/licenses.toml: list it with its licences (SPDX) before depending on it"});
            continue;
        }
        std::vector<std::string> licences;
        if (const auto* list = entry->as_array())
            for (const auto& item : *list)
                if (auto one = item.value<std::string>()) licences.push_back(*one);
        for (const auto& licence : licences) {
            if (licence.starts_with("GPL-") || licence.starts_with("AGPL-"))
                problems.push_back({"licenses", name, std::format("{}: no GPL or AGPL in openmui, dynamically linked or not (D-83)", licence)});
            else if (licence.starts_with("LGPL-") && !(dynamic && dynamic->contains(name)))
                problems.push_back({"licenses", name, std::format("{}: an LGPL library only as an optional plugin, linked dynamically and listed under [dynamic] (D-83)", licence)});
        }
        auto known = index_licences(name);
        if (known && !known->empty()) {
            auto sorted_known = *known, sorted_listed = licences;
            std::ranges::sort(sorted_known);
            std::ranges::sort(sorted_listed);
            if (sorted_known != sorted_listed)
                problems.push_back({"licenses", name, std::format("tools/lint/licenses.toml says {}, the index says {}", python_list(licences), python_list(*known))});
        }
    }
    return problems;
}

// Platforms (D-84): a package that says it runs on the Web (`platforms` lists "emscripten") depends — its
// tests too — only on packages of the workspace that say so as well (CI's Web row tests exactly those).
std::vector<Problem> check_platforms(const Workspace& workspace) {
    std::vector<Problem> problems;
    std::set<std::string> web, known;
    for (const auto& member : workspace.members) {
        if (!member.data) continue;
        known.insert(member.path);
        if (std::ranges::contains(strings_at(table_at(&*member.data, "package"), "platforms"), "emscripten")) web.insert(member.path);
    }
    // A target table counts on the Web only when its selector can choose the Web: `cfg(os = "emscripten")` or a
    // selector that names no system; `cfg(not(os = "emscripten"))` or another system's is native (mcpp#942).
    const auto chooses_web = [](std::string_view selector) {
        if (selector.contains("emscripten")) return !selector.contains("not(");
        return !selector.contains("os =") && !selector.contains("os=");
    };
    for (const auto& path : web) {
        const Member& member = *workspace.find(path);
        std::vector<const toml::table*> tables = tables_of(*member.data, {"dependencies", "dev-dependencies"}, false);
        if (const auto* targets = table_at(&*member.data, "target"))
            for (const auto& [selector, target] : *targets)
                if (chooses_web(selector.str()))
                    for (const auto kind : {"dependencies", "dev-dependencies"}) tables.push_back(table_at(target.as_table(), kind));
        for (const auto* table : tables) {
            if (!table) continue;
            for (const auto& [name, node] : flatten(*table)) {
                const auto relative = path_in(node);
                if (!relative) continue;
                const std::string dependency = resolved(member.path, *relative);
                if (known.contains(dependency) && !web.contains(dependency))
                    problems.push_back({"platforms", member.path,
                                        std::format("runs on the Web (emscripten) but depends on {} ({}), which does not say it does", dependency, name)});
            }
        }
    }
    return problems;
}

// A graph-wide pin of the root ([package.metadata.openmui.pins]) is declared, the same, by every member
// whose graph reaches the package it is for — and by every template's project, which reaches them all.
std::vector<Problem> check_pins(const Workspace& workspace) {
    std::vector<Problem> problems;
    if (!workspace.root_data) return problems;
    const auto* pins = table_at(table_at(table_at(table_at(&*workspace.root_data, "package"), "metadata"), "openmui"), "pins");
    if (!pins || pins->empty()) return problems;
    std::map<std::string, const toml::node*> root_dependencies;
    if (const auto* table = table_at(&*workspace.root_data, "dependencies"))
        for (const auto& [name, node] : flatten(*table)) root_dependencies[name] = node;

    std::map<std::string, std::map<std::string, const toml::node*>> loaded;
    const auto dependencies_of = [&](const std::string& path) -> const std::map<std::string, const toml::node*>& {
        auto [it, fresh] = loaded.try_emplace(path);
        if (!fresh) return it->second;
        const Member* member = workspace.find(path);
        const toml::table* data = member && member->data ? &*member->data : (path == "." ? &*workspace.root_data : nullptr);
        if (!data) return it->second;
        for (const auto* table : {table_at(data, "dependencies"), table_at(data, "dev-dependencies")})
            if (table)
                for (const auto& [name, node] : flatten(*table)) it->second[name] = node;
        if (const auto* targets = table_at(data, "target"))
            for (const auto& [_, target] : *targets)
                if (const auto* table = table_at(target.as_table(), "dependencies"))
                    for (const auto& [name, node] : flatten(*table)) it->second[name] = node;
        return it->second;
    };
    const std::function<bool(const std::string&, const std::string&, std::set<std::string>&)> reaches =
        [&](const std::string& path, const std::string& package, std::set<std::string>& seen) {
            if (!seen.insert(path).second) return false;
            for (const auto& [name, node] : dependencies_of(path)) {
                if (name == package) return true;
                if (const auto relative = path_in(node)) {
                    const std::string dependency = resolved(path, *relative);
                    if (!dependency.empty() && reaches(dependency, package, seen)) return true;
                }
            }
            return false;
        };

    // A template's project depends on openmui itself, so it reaches every pinned package.
    std::vector<std::pair<std::string, toml::table>> templates;
    std::error_code error;
    const fs::path directory = workspace.root / "modules/projects/omui/sdk/templates";
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory, error))
        if (fs::exists(entry.path() / "mcpp.toml.in")) files.push_back(entry.path() / "mcpp.toml.in");
    std::ranges::sort(files);
    for (const auto& file : files) {
        const std::string where = utf8_of(file.lexically_relative(workspace.root));
        // Placeholders as mcpp renders them (`{{project.name}}` → a name).
        static const std::regex placeholder(R"(\{\{[^}]*\}\})");
        const std::string text = std::regex_replace(read_text(file).value_or(""), placeholder, "placeholder");
        try {
            templates.emplace_back(where, toml::parse(text));
        } catch (const toml::parse_error& failure) {
            problems.push_back({"pins", where, std::format("not valid TOML: {}", failure.description())});
        }
    }

    for (const auto& [key, value] : *pins) {
        const std::string pinned(key.str()), package = value.value_or(std::string{});
        const auto want = root_dependencies.find(pinned);
        if (want == root_dependencies.end()) {
            problems.push_back({"pins", "mcpp.toml", std::format("pins `{}` but does not declare it in [dependencies]", pinned)});
            continue;
        }
        const std::string wanted = spelled(want->second);
        for (const auto& [where, data] : templates) {
            std::string have;
            if (const auto* table = table_at(&data, "dependencies"))
                for (const auto& [name, node] : flatten(*table))
                    if (name == pinned) have = spelled(node);
            if (have != wanted) problems.push_back({"pins", where, std::format("an openmui project reaches {}: declare `{} = {}`", package, pinned, wanted)});
        }
        for (const auto& member : workspace.members) {
            std::set<std::string> seen;
            if (!reaches(member.path, package, seen)) continue;
            const auto& own = dependencies_of(member.path);
            const auto have = own.find(pinned);
            const std::string spelled_have = have == own.end() ? std::string{} : spelled(have->second);
            if (spelled_have != wanted)
                problems.push_back({"pins", member.path, std::format("reaches {}: declare `{} = {}` as the root does{}", package, pinned, wanted,
                                                                     have == own.end() ? std::string{} : std::format(" (it has {})", spelled_have))});
        }
    }
    return problems;
}

// Dependency closures that stay small (D-55; plan §5.3 ⑤), by package identity: a package's [dependencies],
// its target dependencies and the feature-deps of the features it is built with (its defaults and what each
// edge asks for), followed through path dependencies.
std::set<std::string> closure_of(const Workspace& workspace, const std::string& member, const std::set<std::string>& asked_first, bool defaults_first) {
    std::set<std::string> names;
    std::set<std::tuple<std::string, std::set<std::string>, bool>> seen;
    std::vector<std::tuple<std::string, std::set<std::string>, bool>> pending{{member, asked_first, defaults_first}};
    while (!pending.empty()) {
        auto [path, asked, defaults] = std::move(pending.back());
        pending.pop_back();
        if (!seen.insert({path, asked, defaults}).second) continue;
        const Member* found = workspace.find(path);
        std::optional<toml::table> read;
        const toml::table* data = found && found->data ? &*found->data : nullptr;
        if (!data) {  // a path dependency outside the members: read it here
            const auto text = read_text(workspace.root / path_of(path) / "mcpp.toml");
            if (!text) continue;
            try {
                read = toml::parse(*text);
            } catch (const toml::parse_error&) {
                continue;
            }
            data = &*read;
        }
        names.insert(package_name(*data));
        std::vector<const toml::table*> tables{table_at(data, "dependencies")};
        if (const auto* targets = table_at(data, "target"))
            for (const auto& [_, target] : *targets) tables.push_back(table_at(target.as_table(), "dependencies"));
        for (const auto feature : features_on(*data, asked, defaults)) tables.push_back(table_at(table_at(data, "feature-deps"), feature));
        for (const auto* table : tables) {
            if (!table) continue;
            for (const auto& [_, node] : flatten(*table)) {
                const auto relative = path_in(node);
                if (!relative) continue;
                const std::string dependency = resolved(path, *relative);
                if (dependency.empty() || !fs::is_regular_file(workspace.root / path_of(dependency) / "mcpp.toml")) continue;
                const auto* spec = spec_of(node);
                const auto features = strings_at(spec, "features");
                const bool own_defaults = !spec || !spec->get("default-features") || spec->get("default-features")->value_or(true);
                pending.emplace_back(dependency, std::set<std::string>(features.begin(), features.end()), own_defaults);
            }
        }
    }
    return names;
}

std::vector<Problem> check_closures(const Workspace& workspace, std::span<const Closure> closures) {
    std::vector<Problem> problems;
    std::map<std::string, std::string> by_name;
    for (const auto& member : workspace.members)
        if (member.data) by_name[package_name(*member.data)] = member.path;
    if (std::ranges::none_of(closures, [&](const Closure& rule) { return by_name.contains(rule.package); })) return problems;  // a tree without them
    for (const auto& rule : closures) {
        const auto found = by_name.find(rule.package);
        if (found == by_name.end()) {
            problems.push_back({"closure", rule.package, "a package the closure rules name is not a member; update the lint's closures (lint_closures.cpp)"});
            continue;
        }
        auto reached = closure_of(workspace, found->second, {}, true);
        reached.erase(rule.package);
        for (const auto& name : reached) {
            const bool extra = !rule.only.empty() ? !std::ranges::contains(rule.only, name)
                                                  : std::ranges::any_of(rule.never, [&](std::string_view prefix) {
                                                        return name == prefix || (name.starts_with(prefix) && name.substr(prefix.size()).starts_with('.'));
                                                    });
            if (extra)
                problems.push_back({"closure", found->second, std::format("reaches {}, which its closure must not (the lint's closures)", name)});
        }
    }
    return problems;
}

std::span<const Closure> repository_closures() {
    // `only`: nothing else may be reached; `never`: none of these (a name, or a name and the packages below it).
    static const std::vector<Closure> table{
        // the render graph: devices, handles and the small tools, nothing more
        {"xrender.graph", {"xrender.graph", "mlibs.rhi", "mlibs.memory", "mlibs.utils"}, {}},
        // drawing vectors needs no effect loader and no shader compiler
        {"xrender.vector", {}, {"xrender.effect", "mlibs.shader"}},
        // text lays out and gives glyphs as geometry; xrender.text draws them
        {"mlibs.text", {}, {"xrender"}},
        // the stepping substrate: the standard library and the small tools only, no solver (D-133)
        {"mlibs.simulation", {"mlibs.simulation", "mlibs.utils"}, {}},
        // the solver core (D-133, D-231): the small tools alone — no UI, game, renderer, GPU, agent kernel or table; what
        // hanim's engine links (the probes tools/ci/hanim-features and hanim-play-consumer hold hanim to that)
        {"mlibs.dynamics", {"mlibs.dynamics", "mlibs.utils"}, {}},
        // the engine's solved simulation: no UI, game, renderer, GPU or compiler (D-133); taken alone, no agent kernel
        // either — a body or a field as an agent's subject is their feature `agent`, so what the engine's kernels that take
        // a family link stays small; what a body is made of (meng.matter's table) is a feature too
        {"meng.dynamics", {}, {"omui", "meng.world", "meng.input", "meng.net", "meng.ui", "xrender", "mlibs.rhi", "mlibs.shader",
                                  "meng.cellular", "mlibs.agent", "mlibs.json", "mlibs.control", "meng.matter"}},
        {"meng.cellular", {}, {"omui", "meng.world", "meng.input", "meng.net", "meng.ui", "xrender", "mlibs.rhi", "mlibs.shader",
                                  "mlibs.agent", "mlibs.json", "mlibs.control", "meng.matter"}},
        // hanim by default is its engine: control and the stage as an agent's subject are features a consumer asks for
        {"sunrisepeak.hanim", {}, {"omui", "mlibs.agent", "mlibs.json", "mlibs.control", "xrender", "meng.matter", "meng.cellular"}},
        // skeletal animation by default is its channels, ACL and the player: glTF (xrender.asset) and the subject are features
        {"hanim.skeleton", {}, {"omui", "xrender", "mlibs.agent", "mlibs.json", "mlibs.control", "meng"}},
        {"meng.cellular.rhi", {}, {"omui", "meng.world", "meng.input", "meng.net", "meng.ui", "xrender"}},
        // the field on screen: the renderer's resources (and what they reach), no UI, game, animation, effect loader or shader compiler
        {"meng.cellular.present", {}, {"omui", "meng.world", "meng.input", "meng.net", "meng.ui", "meng.runtime", "hanim", "sunrisepeak.hanim", "mlibs.shader",
                                          "xrender.effect"}},
        // the agent kernel: below every engine, knowing none of them (D-173)
        {"mlibs.agent", {}, {"omui", "meng", "hanim", "sunrisepeak.hanim", "xrender", "gongfu"}},
        // a game on its own: no interface, no bridge to one (D-162)
        {"meng.runtime", {}, {"omui", "meng.ui"}},
        // the frame's entry: the renderer and the agent kernel, no interface, game or animation (D-162)
        {"xrender.frame", {}, {"omui", "meng", "hanim", "sunrisepeak.hanim"}},
        // three dimensions (M6.3): geometry, numbers and the device's interface — no interface, animation, display list or shader compiler
        {"xrender.scene", {"xrender.scene", "mlibs.geom", "mlibs.math", "mlibs.utils"}, {}},
        {"xrender.mesh", {"xrender.mesh", "xrender.scene", "mlibs.geom", "mlibs.math", "mlibs.utils"}, {}},
        {"xrender.mesh.gpu", {"xrender.mesh.gpu", "xrender.mesh", "xrender.scene", "mlibs.geom", "mlibs.math", "mlibs.utils", "mlibs.rhi", "mlibs.memory"}, {}},
        // … and an application that draws in two dimensions links none of it (a world is shown as a picture its owner drew: meng.ui)
        {"omui.sdk", {}, {"xrender.mesh", "xrender.scene"}},
        {"omui.host.sdl3", {}, {"xrender.mesh", "xrender.scene"}},
        {"omui.host.web", {}, {"xrender.mesh", "xrender.scene"}},
        {"omui.host.headless", {}, {"xrender.mesh", "xrender.scene"}},
        {"omui.site", {}, {"xrender.mesh", "xrender.scene"}},
        {"meng.ui", {}, {"xrender.mesh", "xrender.scene"}},
        // the breakout on its own: its simulation and the runtime, nothing of omui's interface
        {"omui.showcase.widget_breakout.headless", {}, {"omui.core", "omui.view", "omui.semantic", "omui.agent", "omui.viewdoc", "omui.player",
                                                        "omui.designer", "omui.sdk", "omui.host", "omui.ui", "omui.theme", "omui.lang", "meng.ui"}},
    };
    return table;
}

}  // namespace omui::devtools::lint
