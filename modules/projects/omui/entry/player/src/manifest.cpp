// What an application declares in its manifest, read from the manifest as JSON — the one reader of it, on every
// platform: off the Web the mcpp.toml read with toml++ and written as JSON first (native/manifest.cpp); on the Web its
// JSON snapshot, which the page's packer wrote beside it (`mcpp.toml.json`: a page parses no TOML, web/manifest.cpp).
module omui.entry.player;

import std;
import mlibs.json;
import mcpplibs.os;
import omui.player;

namespace omui {

namespace {

// `ui/*.view.xdef.json`: the files of a directory whose names match (`*` in
// the last component only), sorted; a path without `*` as it is.
std::vector<std::filesystem::path> expand(const std::filesystem::path& base, std::string_view pattern) {
    std::vector<std::filesystem::path> out;
    const std::filesystem::path p = std::filesystem::path(std::u8string(pattern.begin(), pattern.end()));
    const std::string name = utf8(p.filename());
    const auto star = name.find('*');
    if (star == std::string::npos) return {p};
    const std::string head = name.substr(0, star), tail = name.substr(star + 1);
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(base / p.parent_path(), ec)) {
        const std::string n = utf8(e.path().filename());
        if (n.size() >= head.size() + tail.size() && n.starts_with(head) && n.ends_with(tail)) out.push_back(p.parent_path() / e.path().filename());
    }
    std::ranges::sort(out);
    return out;
}

}  // namespace

std::expected<player::Manifest, std::string> manifest_of(std::string_view json, const std::filesystem::path& file) {
    auto document = mlibs::json::Document::parse(json, utf8(file));
    if (!document) return std::unexpected(document.error().text());
    const mlibs::json::Node root = document->root();
    player::Manifest m;
    m.base = file.parent_path();
    const mlibs::json::Node t = root.at("/package/metadata/openmui");
    if (!t.is_object()) return std::unexpected(std::format("{}: no [package.metadata.openmui]", utf8(file)));
    if (auto id = t["id"].text()) m.id = *id;
    if (auto title = t["title"].text()) m.title = *title;
    else if (auto name = root.at("/package/name").text()) m.title = *name;
    if (auto size = t["size"].text()) {  // "480x320"
        const std::string_view v = *size;
        const auto x = v.find('x');
        float w = 0, h = 0;
        if (x != std::string_view::npos && std::from_chars(v.data(), v.data() + x, w).ec == std::errc{} &&
            std::from_chars(v.data() + x + 1, v.data() + v.size(), h).ec == std::errc{} && w > 0 && h > 0)
            m.size = {w, h};
        else
            return std::unexpected(std::format("{}: size is \"<width>x<height>\", not \"{}\"", utf8(file), v));
    }
    auto paths = [&](std::string_view key, std::vector<std::filesystem::path>& out) {
        for (const mlibs::json::Node& v : t[key].items())
            if (auto s = v.text())
                for (auto& p : expand(m.base, *s)) out.push_back(std::move(p));
    };
    paths("components", m.components);
    paths("views", m.views);
    paths("themes", m.themes);
    // effects = "fx": the application's own effects, a directory.
    if (auto e = t["effects"].text()) m.effects = m.base / std::filesystem::path(std::u8string(e->begin(), e->end()));
    // theme = ["dark", "compact"]: the composition to start with.
    for (const mlibs::json::Node& v : t["theme"].items())
        if (auto s = v.text()) m.theme.push_back(*s);
    // [package.metadata.openmui.scripts]: name = "logic/main.ts", or
    // name = { lang = "ts", entry = "logic/main.ts" }; run in the names' order.
    for (const auto& [name, v] : t["scripts"].members()) {
        std::optional<std::string> lang, entry = v.text();
        if (v.is_object()) {
            lang = v["lang"].text();
            entry = v["entry"].text();
        }
        if (!entry) return std::unexpected(std::format("{}: the script `{}` names its `entry`", utf8(file), name));
        m.scripts.push_back({lang.value_or(""), std::filesystem::path(std::u8string(entry->begin(), entry->end()))});
    }
    return m;
}

}  // namespace omui
