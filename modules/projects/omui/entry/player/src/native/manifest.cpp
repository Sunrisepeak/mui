// The application's manifest off the Web: its mcpp.toml read with toml++ — the one unit that catches what the library
// throws, to answer std::expected (AGENTS §4: a native unit adapts its third-party library) — and written as JSON for
// the one reader of the declaration (manifest.cpp). A `.json` manifest (a page's snapshot) is read as it is.
module omui.entry.player;

import std;
import tomlplusplus;
import mcpplibs.os;
import omui.player;

namespace omui {

std::expected<player::Manifest, std::string> read_manifest(const std::filesystem::path& file) {
    const std::string name = mcpplibs::os::fs::utf8(file);
    if (file.extension() == ".json") {
        auto text = mcpplibs::os::fs::read(file);
        if (!text) return std::unexpected(std::format("{}: {}", name, text.error().message()));
        return manifest_of(*text, file);
    }
    toml::table root;
    try {
        root = toml::parse_file(name);
    } catch (const toml::parse_error& e) {
        return std::unexpected(std::format("{}:{}:{}: {}", name, e.source().begin.line, e.source().begin.column, e.description()));
    }
    std::ostringstream json;
    json << toml::json_formatter{root};
    return manifest_of(json.str(), file);
}

}  // namespace omui
