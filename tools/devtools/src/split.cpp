module omui.devtools.split;

import std;
import mcpplibs.os;
import mlibs.json;
import omui.devtools.members;
import omui.devtools.run;

namespace omui::devtools::split {

namespace {

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

bool under(std::string_view path, std::string_view directory) {
    return path == directory || (path.starts_with(directory) && path.size() > directory.size() && path[directory.size()] == '/');
}

// Tracked files from `root` (a submodule's from its own index, prefixed with its path).
std::expected<std::vector<std::string>, std::string> tracked(const std::filesystem::path& root, std::string_view prefix = {}) {
    auto listed = run::output(prefix.empty() ? root : root / mcpplibs::os::fs::from_utf8(prefix), "git", {"ls-files", "-z"});
    if (!listed) return std::unexpected(listed.error());
    std::vector<std::string> paths;
    for (const auto part : std::views::split(*listed, '\0'))
        if (!part.empty()) paths.push_back(prefix.empty() ? std::string(part.begin(), part.end()) : std::format("{}/{}", prefix, std::string_view(part.begin(), part.end())));
    return paths;
}

// The repository's own files every tree carries: the toolchain's pins, the formatting, the licence, the root
// markers tests find the repository by.
constexpr std::array root_files{".xlings.json", ".clang-format", ".gitignore", ".gitmodules", "AGENTS.md", "LICENSE", "README.md"};

// The foundation's directories besides its projects: mlibs' groups and the repository's maintenance tools.
constexpr std::array foundation_roots{"modules/base", "modules/gfx", "modules/content", "modules/media", "tools/devtools"};

// Each project's directory and its tier's rank, from its identity (<name>.project.xdef.json, D-208, D-231).
std::expected<std::vector<std::pair<std::string, std::size_t>>, std::string> project_tiers(const std::filesystem::path& root) {
    std::vector<std::pair<std::string, std::size_t>> out;
    std::error_code error;
    for (const auto& child : std::filesystem::directory_iterator(root / "modules" / "projects", error)) {
        if (!child.is_directory(error)) continue;
        const std::string leaf = mcpplibs::os::fs::utf8(child.path().filename());
        const std::string name = std::format("modules/projects/{0}/{0}.project.xdef.json", leaf);
        const auto document = mlibs::json::Document::parse(read(root / mcpplibs::os::fs::from_utf8(name)), name);
        const auto tier = document ? document->root()["project"]["tier"].text() : std::nullopt;
        const auto found = tier ? std::ranges::find(tiers, *tier) : tiers.end();
        if (found == tiers.end()) return std::unexpected(std::format("{} names no tier (lint project-dag)", name));
        out.emplace_back(std::format("modules/projects/{}", leaf), static_cast<std::size_t>(found - tiers.begin()));
        // omui's build plugins (mcpp/) are of omui's tier, outside its directory (lint project-dag).
        if (leaf == "omui") out.emplace_back("mcpp", static_cast<std::size_t>(found - tiers.begin()));
    }
    std::ranges::sort(out);
    return out;
}

}  // namespace

std::expected<Plan, std::string> plan(const std::filesystem::path& root, std::string_view project) {
    const std::string manifest = read(root / "mcpp.toml");
    if (manifest.empty()) return std::unexpected("no mcpp.toml at the root");
    std::vector<std::string> all;
    for (auto& path : members::listed(manifest)) {
        if (!path.ends_with("/*")) {
            all.push_back(std::move(path));
            continue;
        }
        std::error_code error;
        std::vector<std::string> children;
        for (const auto& child : std::filesystem::directory_iterator(root / mcpplibs::os::fs::from_utf8(path.substr(0, path.size() - 2)), error))
            if (std::filesystem::exists(child.path() / "mcpp.toml", error)) children.push_back(mcpplibs::os::fs::generic_utf8(child.path().lexically_relative(root)));
        std::ranges::sort(children);
        all.insert(all.end(), children.begin(), children.end());
    }
    std::map<std::string, std::vector<std::string>> uses;
    for (const auto& member : all) uses[member] = members::uses_of(member, read(root / mcpplibs::os::fs::from_utf8(member) / "mcpp.toml"), all);
    std::set<std::string> chosen;
    std::vector<std::string> pending;
    // A tier: its directories and the tiers' below; else one directory.
    const auto tier = std::ranges::find(tiers, project);
    std::vector<std::pair<std::string, std::size_t>> ranks;
    std::vector<std::string> seeds;
    if (tier != tiers.end()) {
        auto read_ranks = project_tiers(root);
        if (!read_ranks) return std::unexpected(read_ranks.error());
        ranks = std::move(*read_ranks);
        seeds.assign(foundation_roots.begin(), foundation_roots.end());
        for (const auto& [directory, rank] : ranks)
            if (rank <= static_cast<std::size_t>(tier - tiers.begin())) seeds.push_back(directory);
    } else {
        seeds.emplace_back(project);
    }
    for (const auto& member : all)
        if (std::ranges::any_of(seeds, [&](const std::string& seed) { return under(member, seed); })) pending.push_back(member);
    if (pending.empty()) return std::unexpected(std::format("no member under {}", project));
    std::map<std::string, std::string> reached_from;  // a member and the one that first reached it
    while (!pending.empty()) {
        auto member = std::move(pending.back());
        pending.pop_back();
        if (!chosen.insert(member).second) continue;
        for (const auto& used : uses[member]) {
            reached_from.try_emplace(used, member);
            pending.push_back(used);
        }
    }
    // A tier's tree holds nothing of a tier above (D-231): what reached it is named.
    if (tier != tiers.end())
        for (const auto& member : chosen)
            for (const auto& [directory, rank] : ranks)
                if (under(member, directory) && rank > static_cast<std::size_t>(tier - tiers.begin())) {
                    std::string chain = member;
                    for (auto at = reached_from.find(member); at != reached_from.end(); at = reached_from.find(at->second)) {
                        chain = at->second + " → " + chain;
                        if (chain.size() > 2000) break;
                    }
                    return std::unexpected(std::format("the {} tier reaches {}, of the {} tier: {}", project, member, tiers[rank], chain));
                }
    Plan out;
    for (const auto& member : all)
        if (chosen.contains(member)) out.members.push_back(member);
    auto files = tracked(root);
    if (!files) return std::unexpected(files.error());
    if (chosen.contains("spec")) {
        auto spec = tracked(root, "spec");
        if (!spec) return std::unexpected(spec.error());
        files->insert(files->end(), spec->begin(), spec->end());
    }
    // A file is its innermost member's (a package nested in another's directory owns its own files): taken when that
    // member is, so the tree holds no manifest its workspace does not list (red 126).
    for (const auto& file : *files) {
        if (std::ranges::contains(root_files, file)) {
            out.files.push_back(file);
            continue;
        }
        const std::string* owner = nullptr;
        for (const auto& member : all)
            if (under(file, member) && (!owner || member.size() > owner->size())) owner = &member;
        if (owner && chosen.contains(*owner)) out.files.push_back(file);
    }
    return out;
}

std::expected<void, std::string> write(const std::filesystem::path& root, const Plan& plan, const std::filesystem::path& out) {
    std::error_code error;
    std::filesystem::remove_all(out, error);
    for (const auto& file : plan.files) {
        const auto from = root / mcpplibs::os::fs::from_utf8(file), to = out / mcpplibs::os::fs::from_utf8(file);
        if (std::filesystem::is_directory(from, error)) continue;  // a submodule's entry in the index
        std::filesystem::create_directories(to.parent_path(), error);
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, error);
        if (error) return std::unexpected(std::format("{}: {}", file, error.message()));
    }
    // The root manifest as it is, its members only those the tree holds.
    const std::string manifest = read(root / "mcpp.toml");
    const auto space = manifest.find("\n[workspace]");
    const auto start = space == std::string::npos ? space : manifest.find("\nmembers", space);
    const auto open = start == std::string::npos ? start : manifest.find('[', start);
    const auto close = open == std::string::npos ? open : manifest.find(']', open);
    if (close == std::string::npos) return std::unexpected("the root manifest's [workspace] members are not a list");
    std::string listed = "[\n";
    for (const auto& member : plan.members) listed += std::format("    \"{}\",\n", member);
    listed += "]";
    std::ofstream(out / "mcpp.toml", std::ios::binary) << manifest.substr(0, open) << listed << manifest.substr(close + 1);
    return {};
}

}  // namespace omui::devtools::split
