module omui.devtools.members;

import std;

namespace omui::devtools::members {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// Every double-quoted string on a line, before a comment.
std::vector<std::string> quoted(std::string_view line) {
    std::vector<std::string> found;
    for (std::size_t at = 0; at < line.size(); ++at) {
        if (line[at] == '#') break;
        if (line[at] != '"') continue;
        const auto end = line.find('"', at + 1);
        if (end == std::string_view::npos) break;
        found.emplace_back(line.substr(at + 1, end - at - 1));
        at = end;
    }
    return found;
}

// "a/b/../c" → "a/c"; "./a" → "a".
std::string normal(std::string_view path) {
    std::vector<std::string_view> parts;
    for (const auto part : std::views::split(path, '/')) {
        const std::string_view name(part.begin(), part.end());
        if (name.empty() || name == ".") continue;
        if (name == ".." && !parts.empty() && parts.back() != "..") parts.pop_back();
        else parts.push_back(name);
    }
    std::string joined;
    for (const auto name : parts) {
        if (!joined.empty()) joined += '/';
        joined += name;
    }
    return joined;
}

}  // namespace

std::vector<std::string> listed(std::string_view root_manifest) {
    std::vector<std::string> paths;
    bool in_workspace = false, in_members = false;
    for (const auto part : std::views::split(root_manifest, '\n')) {
        const std::string_view line = trim(std::string_view(part.begin(), part.end()));
        if (line.starts_with('[') && !in_members) {
            in_workspace = line == "[workspace]";
            continue;
        }
        if (!in_workspace) continue;
        if (!in_members && line.starts_with("members")) {
            in_members = true;
            const auto open = line.find('[');
            if (open == std::string_view::npos) continue;
            for (auto& path : quoted(line.substr(open))) paths.push_back(std::move(path));
            if (line.find(']', open) != std::string_view::npos) in_members = false;
            continue;
        }
        if (in_members) {
            const auto close = line.find(']');
            for (auto& path : quoted(line.substr(0, close))) paths.push_back(std::move(path));
            if (close != std::string_view::npos) in_members = false;
        }
    }
    return paths;
}

std::vector<std::string> uses_of(std::string_view member, std::string_view manifest, std::span<const std::string> all) {
    std::vector<std::string> uses;
    for (const auto part : std::views::split(manifest, '\n')) {
        const std::string_view line(part.begin(), part.end());
        const auto key = line.find("path");
        if (key == std::string_view::npos || line.find('{') == std::string_view::npos) continue;
        const auto equals = line.find('=', key);
        if (equals == std::string_view::npos) continue;
        const auto values = quoted(line.substr(equals));
        if (values.empty()) continue;
        const std::string target = normal(std::string(member) + "/" + values.front());
        if (std::ranges::contains(all, target) && !std::ranges::contains(uses, target)) uses.push_back(target);
    }
    return uses;
}

std::string owner_of(std::string_view file, std::span<const Member> all) {
    std::string best;
    for (const Member& member : all)
        if ((file == member.path || (file.starts_with(member.path) && file.size() > member.path.size() && file[member.path.size()] == '/')) &&
            member.path.size() > best.size())
            best = member.path;
    return best;
}

std::vector<std::string> with_consumers(std::span<const std::string> chosen, std::span<const Member> all) {
    std::vector<std::string> picked;
    for (const Member& member : all) {
        const bool own = std::ranges::contains(chosen, member.path);
        const bool uses = std::ranges::any_of(member.uses, [&](const std::string& used) { return std::ranges::contains(chosen, used); });
        if (own || uses) picked.push_back(member.path);
    }
    return picked;
}


std::vector<std::vector<std::string>> blocks(std::span<const std::string> members, std::size_t count) {
    std::vector<std::vector<std::string>> split;
    if (members.empty()) return split;
    count = std::clamp<std::size_t>(count, 1, members.size());
    const std::size_t base = members.size() / count, larger = members.size() % count;
    std::size_t at = 0;
    for (std::size_t block = 0; block < count; ++block) {
        const std::size_t size = base + (block < larger ? 1 : 0);
        split.emplace_back(members.begin() + static_cast<std::ptrdiff_t>(at), members.begin() + static_cast<std::ptrdiff_t>(at + size));
        at += size;
    }
    return split;
}

std::string cover_problem(std::span<const std::string> members, std::span<const std::vector<std::string>> blocks) {
    std::map<std::string_view, int> seen;
    for (const auto& member : members) seen[member] = 0;
    for (const auto& block : blocks) {
        for (const auto& member : block) {
            const auto found = seen.find(member);
            if (found == seen.end()) return std::format("{} is in a block but not a member", member);
            if (++found->second > 1) return std::format("{} is in two blocks", member);
        }
    }
    for (const auto& member : members)
        if (seen[member] == 0) return std::format("{} is in no block", member);
    return {};
}

}  // namespace omui::devtools::members
