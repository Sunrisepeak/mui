// mlibs.project: profiles as their owners declare them (D-207), read from their snapshots.
module mlibs.project;

import std;
import mcpplibs.os;
import mlibs.json;
import mlibs.xdef;

namespace mlibs::project {

namespace {

// `*` within a directory, `**` across directories, anything else itself.
bool matches(std::string_view glob, std::string_view path) {
    if (glob.empty()) return path.empty();
    if (glob.starts_with("**")) {
        auto rest = glob.substr(2);
        // `**/x`: x at the start of any directory's entry; `**x`: anything, then x.
        const bool whole = rest.starts_with('/');
        if (whole) rest.remove_prefix(1);
        for (std::size_t at = 0; at <= path.size(); ++at)
            if ((!whole || at == 0 || path[at - 1] == '/') && matches(rest, path.substr(at))) return true;
        return false;
    }
    if (glob.front() == '*') {
        for (std::size_t at = 0; at <= path.size(); ++at) {
            if (matches(glob.substr(1), path.substr(at))) return true;
            if (at < path.size() && path[at] == '/') break;
        }
        return false;
    }
    return !path.empty() && glob.front() == path.front() && matches(glob.substr(1), path.substr(1));
}

using Condition = std::function<bool(const Package&)>;

std::expected<Condition, std::string> condition_of(const mlibs::json::Node& node) {
    if (const auto keys = node["metadata"]; keys) {
        std::vector<std::string> wanted;
        for (const auto& key : keys.items())
            if (auto text = key.text()) wanted.push_back(std::move(*text));
        return Condition{[wanted](const Package& package) { return std::ranges::all_of(wanted, [&](const std::string& key) { return package.metadata.contains(key); }); }};
    }
    if (const auto glob = node["file"].text()) {
        return Condition{[glob = *glob](const Package& package) { return std::ranges::any_of(package.files, [&](const std::string& file) { return matches(glob, file); }); }};
    }
    if (const auto name = node["dependsOn"].text()) {
        return Condition{[name = *name](const Package& package) { return std::ranges::contains(package.dependencies, name); }};
    }
    if (const auto any = node["anyOf"]; any.is_array()) {
        std::vector<Condition> each;
        for (const auto& item : any.items()) {
            auto one = condition_of(item);
            if (!one) return std::unexpected(one.error());
            each.push_back(std::move(*one));
        }
        return Condition{[each](const Package& package) { return std::ranges::any_of(each, [&](const Condition& one) { return one(package); }); }};
    }
    return std::unexpected(std::format("{}: no condition (metadata, file, dependsOn or anyOf)", node.pointer()));
}

}  // namespace

std::expected<std::vector<Profile>, std::string> profiles_from(std::string_view json, std::string_view file) {
    const auto document = mlibs::json::Document::parse(json, std::string(file));
    if (!document) return std::unexpected(document.error().text());
    const auto root = document->root();
    if (const auto wrong = mlibs::xdef::format_problem(root["$format"].text(), "profile", 1)) return std::unexpected(std::format("{}: {}", file, *wrong));
    std::vector<Profile> out;
    for (const auto& [name, declared] : root["profile"].members()) {
        Profile profile{name, declared["doc"].text().value_or(""), {}};
        for (const auto& rule : declared["rules"].items()) {
            auto holds = condition_of(rule);
            if (!holds) return std::unexpected(std::format("{}: {}", file, holds.error()));
            const std::string id = rule["id"].text().value_or(name + ".rule");
            const std::string doc = rule["doc"].text().value_or(id), fix = rule["fix"].text().value_or("");
            profile.rules.push_back({id, doc, [id, doc, fix, holds = std::move(*holds)](const Project&, const Package& package, std::vector<Finding>& out) {
                if (holds(package)) return;
                out.push_back({id, package.relative.empty() ? std::string("mcpp.toml") : package.relative + "/mcpp.toml", doc, fix});
            }});
        }
        out.push_back(std::move(profile));
    }
    return out;
}

std::expected<std::vector<Profile>, std::string> profiles_in(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (auto walker = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
         walker != std::filesystem::recursive_directory_iterator(); walker.increment(error)) {
        if (error) break;
        const auto leaf = mcpplibs::os::fs::utf8(walker->path().filename());
        if (walker->is_directory(error)) {
            if (leaf.starts_with('.') || leaf == "target" || leaf == "node_modules" || leaf == "fixtures" ||
                (walker->path() != root && std::filesystem::exists(walker->path() / ".git", error)))
                walker.disable_recursion_pending();
            continue;
        }
        if (leaf.ends_with(".profile.xdef.json")) files.push_back(walker->path());
    }
    std::ranges::sort(files);
    std::vector<Profile> out;
    for (const auto& file : files) {
        const auto name = mcpplibs::os::fs::generic_utf8(file.lexically_relative(root));
        const auto text = mcpplibs::os::fs::read(file);
        if (!text) return std::unexpected(std::format("{}: cannot be read", name));
        auto read = profiles_from(*text, name);
        if (!read) return std::unexpected(read.error());
        std::ranges::move(*read, std::back_inserter(out));
    }
    return out;
}

}  // namespace mlibs::project
