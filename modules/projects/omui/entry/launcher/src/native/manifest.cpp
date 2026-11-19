// What an application declares of its runtime, read from its manifest with toml++ (a native unit: the library's
// parse error caught here, D-56).
module omui.entry.launcher;

import std;
import tomlplusplus;

namespace omui::launcher {

std::expected<Declared, std::string> declared(std::string_view manifest) {
    toml::table table;
    try {
        table = toml::parse(manifest);
    } catch (const toml::parse_error& error) {
        return std::unexpected(std::format("mcpp.toml:{}:{}: {}", error.source().begin.line, error.source().begin.column, error.description()));
    }
    const toml::node_view section = table["package"]["metadata"]["openmui"]["runtime"];
    if (!section.is_table())
        return std::unexpected(std::string("mcpp.toml declares no runtime: [package.metadata.openmui.runtime] with openmui = \"0.4\" (the specification "
                                           "its scripts are written against)"));
    Declared out;
    out.openmui = section["openmui"].value_or(std::string());
    if (out.openmui.empty() || out.openmui.find('.') == std::string::npos)
        return std::unexpected(std::string("[package.metadata.openmui.runtime] openmui is the specification's major and minor version: openmui = \"0.4\""));
    if (const toml::array* needs = section["needs"].as_array())
        for (const toml::node& need : *needs) {
            const auto name = need.value<std::string>();
            if (!name) return std::unexpected(std::string("[package.metadata.openmui.runtime] needs is a list of capabilities: needs = [\"agent\"]"));
            out.needs.push_back(*name);
        }
    out.runtime = section["runtime"].value_or(std::string());
    if (!out.runtime.empty() && !out.runtime.starts_with('='))
        return std::unexpected(std::format("[package.metadata.openmui.runtime] runtime pins one version exactly: runtime = \"={}\"", out.runtime));
    return out;
}

}  // namespace omui::launcher
