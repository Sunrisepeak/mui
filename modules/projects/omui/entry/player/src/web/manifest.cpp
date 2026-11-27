// The application's manifest on the Web: the JSON snapshot its page's packer wrote beside it (omui-gen dist-toy writes
// a TOML file the page carries as `<file>.json`: a page parses no TOML), read by the one reader of the declaration
// (manifest.cpp). No TOML parser is in a Web program.
module omui.entry.player;

import std;
import mcpplibs.os;
import omui.player;

namespace omui {

std::expected<player::Manifest, std::string> read_manifest(const std::filesystem::path& file) {
    std::filesystem::path snapshot = file;
    if (file.extension() != ".json") snapshot += ".json";
    auto text = mcpplibs::os::fs::read(snapshot);
    if (!text)
        return std::unexpected(std::format("{}: {} (a page carries an application's mcpp.toml as its JSON snapshot, which `omui build --web` writes)",
                                           mcpplibs::os::fs::utf8(snapshot), text.error().message()));
    return manifest_of(*text, file);
}

}  // namespace omui
