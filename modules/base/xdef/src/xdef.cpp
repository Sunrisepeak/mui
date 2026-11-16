// mlibs.xdef: names, format identifiers, snapshots and the checker (the manifests are read in native/).
module mlibs.xdef;

import std;
import mcpplibs.os;
import mlibs.ts;

namespace mlibs::xdef {

namespace {

bool is_word(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

constexpr std::array skipped{"target", ".git", ".mcpp", ".xlings", ".cache", "build", "node_modules", "fixtures", ".venv", "__pycache__", "spec"};

}  // namespace

std::optional<FileName> parse_name(std::string_view file_name) {
    std::vector<std::string_view> parts;
    for (const auto part : std::views::split(file_name, '.')) parts.emplace_back(part.begin(), part.end());
    if (parts.size() < 4) return std::nullopt;
    const auto extension = parts[parts.size() - 1];
    const auto suffix = parts[parts.size() - 2];
    const auto kind = parts[parts.size() - 3];
    if (extension != "ts" && extension != "json" && extension != "jsonl") return std::nullopt;
    if (suffix != container && suffix != earlier_container) return std::nullopt;
    if (!is_word(kind)) return std::nullopt;
    const auto name = file_name.substr(0, file_name.size() - kind.size() - suffix.size() - extension.size() - 3);
    if (name.empty()) return std::nullopt;
    return FileName{std::string(name), std::string(kind), std::string(suffix), std::string(extension)};
}

std::optional<Format> parse_format(std::string_view text) {
    const auto at = text.rfind('@');
    if (at == std::string_view::npos || !is_word(text.substr(0, at))) return std::nullopt;
    int major = 0;
    const auto digits = text.substr(at + 1);
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), major);
    if (error != std::errc{} || end != digits.data() + digits.size() || major < 1) return std::nullopt;
    return Format{std::string(text.substr(0, at)), major};
}

std::string format_id(const Format& format) { return std::format("{}@{}", format.kind, format.major); }

const Kind* Registry::find(std::string_view kind) const {
    const auto found = std::ranges::find(kinds, kind, &Kind::name);
    return found == kinds.end() ? nullptr : &*found;
}

std::vector<mlibs::ts::NamedModule> Registry::define_modules() const {
    std::map<std::string, std::vector<std::string_view>> by_module;
    for (const auto& kind : kinds)
        if (!kind.module.empty()) by_module[kind.module].push_back(kind.name);
    std::vector<mlibs::ts::NamedModule> out;
    for (const auto& [module, names] : by_module)
        out.push_back({module, std::format("// {}: its owner's kinds (mlibs.xdef registry).\n", module) + mlibs::ts::define_module(names)});
    return out;
}

std::optional<std::filesystem::path> workspace_of(const std::filesystem::path& path) {
    std::error_code error;
    auto here = std::filesystem::absolute(path, error).lexically_normal();
    if (!std::filesystem::is_directory(here, error)) here = here.parent_path();
    for (; !here.empty(); here = here.parent_path()) {
        std::ifstream in(here / "mcpp.toml", std::ios::binary);
        if (in) {
            const std::string text{std::istreambuf_iterator<char>(in), {}};
            if (text.starts_with("[workspace]") || text.contains("\n[workspace]")) return here;
        }
        if (here == here.parent_path()) break;
    }
    return std::nullopt;
}

std::expected<std::string, mlibs::ts::TsFailure> snapshot(const std::filesystem::path& file, const Registry& registry) {
    auto data = mlibs::ts::evaluate(file, {.modules = registry.define_modules()});
    if (!data) return std::unexpected(std::move(data.error()));
    return std::format("{{\"{}\":{}}}\n", data->kind, data->json);
}

std::vector<Problem> check(const std::filesystem::path& root, const Registry& registry) {
    std::vector<Problem> problems;
    for (const auto& problem : registry.problems) problems.push_back({"mcpp.toml", problem});
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (auto walker = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
         walker != std::filesystem::recursive_directory_iterator(); walker.increment(error)) {
        if (error) break;
        const auto leaf = mcpplibs::os::fs::utf8(walker->path().filename());
        if (walker->is_directory(error)) {
            if (std::ranges::contains(skipped, leaf) || (leaf.starts_with('.') && leaf != ".agents")) walker.disable_recursion_pending();
            continue;
        }
        if (parse_name(leaf)) files.push_back(walker->path());
    }
    std::ranges::sort(files);
    for (const auto& file : files) {
        const auto relative = mcpplibs::os::fs::generic_utf8(file.lexically_relative(root));
        const auto name = *parse_name(mcpplibs::os::fs::utf8(file.filename()));
        const Kind* kind = registry.find(name.kind);
        if (!kind) {
            problems.push_back({relative, std::format("a {} file, and no package registers the kind {} ([[package.metadata.openmui.xdef]] kinds)",
                                                      name.kind, name.kind)});
            continue;
        }
        if (name.extension != "ts") continue;
        // A module with a committed snapshot beside it: the snapshot is what the module says now.
        auto sibling = file;
        sibling.replace_extension(".json");
        std::ifstream in(sibling, std::ios::binary);
        if (!in) continue;
        const std::string committed{std::istreambuf_iterator<char>(in), {}};
        auto made = snapshot(file, registry);
        if (!made) {
            problems.push_back({relative, made.error().text()});
            continue;
        }
        if (*made != committed)
            problems.push_back({mcpplibs::os::fs::generic_utf8(sibling.lexically_relative(root)),
                                std::format("is not what {} evaluates to now: `xdef snapshot {} --write`", mcpplibs::os::fs::utf8(file.filename()), relative)});
    }
    return problems;
}

}  // namespace mlibs::xdef
