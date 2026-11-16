// The kinds' registry read from a workspace's manifests (mlibs.xdef): toml++ reports a manifest that is not
// TOML by throwing, so it is read here, in a native unit, and the error becomes a problem.
module mlibs.xdef;

import std;
import mcpplibs.os;
import tomlplusplus;

namespace mlibs::xdef {

namespace {

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

// The members a root manifest lists, `dir/*` as the children that hold a manifest.
std::vector<std::filesystem::path> members(const std::filesystem::path& workspace, const toml::table& root) {
    std::vector<std::filesystem::path> out;
    const auto* list = root["workspace"]["members"].as_array();
    if (!list) return out;
    for (const auto& entry : *list) {
        const auto pattern = entry.value<std::string>();
        if (!pattern) continue;
        if (pattern->ends_with("/*")) {
            std::vector<std::filesystem::path> children;
            std::error_code error;
            for (const auto& child : std::filesystem::directory_iterator(workspace / mcpplibs::os::fs::from_utf8(pattern->substr(0, pattern->size() - 2)), error))
                if (std::filesystem::exists(child.path() / "mcpp.toml", error)) children.push_back(child.path());
            std::ranges::sort(children);
            out.insert(out.end(), children.begin(), children.end());
        } else {
            out.push_back(workspace / mcpplibs::os::fs::from_utf8(*pattern));
        }
    }
    return out;
}

std::string package_name(const toml::table& manifest) {
    const auto space = manifest["package"]["namespace"].value<std::string>();
    const auto name = manifest["package"]["name"].value<std::string>().value_or("?");
    return space ? std::format("{}.{}", *space, name) : name;
}

}  // namespace

std::expected<Registry, std::string> Registry::read(const std::filesystem::path& workspace) {
    std::string problem;
    const auto root = parsed(workspace / "mcpp.toml", problem);
    if (!root) return std::unexpected(problem);
    Registry registry;
    std::map<std::string, std::string> owners;  // kind → its owner, for the one-owner rule
    for (const auto& member : members(workspace, *root)) {
        auto manifest = parsed(member / "mcpp.toml", problem);
        if (!manifest) continue;  // a member that is not TOML is the lint's (toml)
        const auto* entry = manifest->at_path("package.metadata.openmui.xdef").as_table();
        if (!entry) continue;
        const std::string owner = package_name(*manifest);
        const auto* kinds = (*entry)["kinds"].as_table();
        if (!kinds) {
            registry.problems.push_back(std::format("{}: [package.metadata.openmui.xdef] has `kinds = {{ <kind> = <major> }}`", owner));
            continue;
        }
        std::map<std::string, std::string> module_of;  // kind → its define module
        if (const auto* modules = (*entry)["modules"].as_table())
            for (const auto& [module, list] : *modules)
                if (const auto* names = list.as_array())
                    for (const auto& name : *names)
                        if (const auto kind = name.value<std::string>()) module_of[*kind] = std::string(module.str());
        for (const auto& [key, value] : *kinds) {
            const std::string kind(key.str());
            const auto major = value.value<std::int64_t>();
            if (!major || *major < 1) {
                registry.problems.push_back(std::format("{}: the kind {} has no major version (a whole number from 1)", owner, kind));
                continue;
            }
            if (const auto [at, added] = owners.emplace(kind, owner); !added) {
                registry.problems.push_back(std::format("the kind {} has two owners: {} and {} (one kind, one owner)", kind, at->second, owner));
                continue;
            }
            const auto module = module_of.find(kind);
            registry.kinds.push_back({kind, static_cast<int>(*major), owner, module == module_of.end() ? std::string() : module->second});
            if (module != module_of.end()) module_of.erase(module);
        }
        for (const auto& [kind, module] : module_of)
            registry.problems.push_back(std::format("{}: {} defines {}, which is not among its kinds", owner, module, kind));
    }
    return registry;
}

}  // namespace mlibs::xdef
