module openmui.build.plan;

import std;
import mcpp;

namespace openmui::build {

bool submit(std::span<const edge> edges) {
    for (const edge& e : edges) {
        mcpp::action a;
        a.id = e.id.c_str();
        a.role = mcpp::roles::source;
        a.description = e.description.c_str();
        for (const auto& c : e.command) a.arg(c.c_str());
        for (const auto& i : e.inputs) a.input(i.c_str());
        for (const auto& o : e.outputs) a.output(o.c_str());
        for (const auto& p : e.provides) a.provides(p.c_str());
        for (const auto& i : e.imports) a.imports(i.c_str());
        a.submit();
    }
    return true;
}

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string generic_utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string generator() { return std::string(mcpp::dep_bin("openmui.gen", "omui-gen")); }

std::string namespace_of(std::string_view module) {
    std::string ns;
    for (char c : module) ns += c == '.' ? std::string("::") : std::string(1, c);
    return ns;
}

std::vector<std::filesystem::path> files_ending(const std::filesystem::path& dir, std::string_view suffix) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (utf8(e.path()).ends_with(suffix)) files.push_back(e.path());
    std::ranges::sort(files);
    return files;
}

std::vector<std::string> files_under(const std::filesystem::path& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir, ec))
        if (e.is_regular_file()) files.push_back(utf8(e.path()));
    std::ranges::sort(files);
    return files;
}

bool is_packed() {
    const std::string_view stage = mcpp::pack_stage_dir();
    if (stage.empty()) return true;
    const std::filesystem::path dir{stage};
    return generic_utf8(dir.filename()) == std::format("{}-{}-{}", mcpp::package_name(), mcpp::package_version(), mcpp::target());
}

std::vector<std::string> table_names(const std::filesystem::path& file, std::string_view table) {
    const std::string open = std::format("[{}.", table);
    std::vector<std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.starts_with(open)) continue;
        const auto end = line.find(']');
        if (end == std::string::npos) continue;
        const std::string name = line.substr(open.size(), end - open.size());
        if (name.find('.') == std::string::npos) out.push_back(name);
    }
    return out;
}

}  // namespace openmui::build
