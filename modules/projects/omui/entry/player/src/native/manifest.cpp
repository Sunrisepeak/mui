// The application's manifest read with toml++ — the one unit that catches what the library throws, to answer
// std::expected (AGENTS §4: a native unit adapts its third-party library).
module omui.entry.player;

import std;
import tomlplusplus;
import mcpplibs.os;
import omui.player;

namespace omui {

namespace {
std::string utf8(const std::filesystem::path& path) { return mcpplibs::os::fs::utf8(path); }

// `ui/*.view.omui.json`: the files of a directory whose names match (`*` in
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

std::expected<player::Manifest, std::string> read_manifest(const std::filesystem::path& file) {
    toml::table root;
    try {
        root = toml::parse_file(utf8(file));
    } catch (const toml::parse_error& e) {
        return std::unexpected(std::format("{}:{}:{}: {}", utf8(file), e.source().begin.line, e.source().begin.column, e.description()));
    }
    player::Manifest m;
    m.base = file.parent_path();
    const toml::table* meta = root["package"]["metadata"]["openmui"].as_table();
    if (!meta) return std::unexpected(std::format("{}: no [package.metadata.openmui]", utf8(file)));
    const toml::table& t = *meta;
    if (auto id = t["id"].value<std::string>()) m.id = *id;
    if (auto title = t["title"].value<std::string>()) m.title = *title;
    else if (auto name = root["package"]["name"].value<std::string>()) m.title = *name;
    if (auto size = t["size"].value<std::string>()) {  // "480x320"
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
        if (const toml::array* a = t[key].as_array())
            for (const auto& v : *a)
                if (auto s = v.value<std::string>())
                    for (auto& p : expand(m.base, *s)) out.push_back(std::move(p));
    };
    paths("components", m.components);
    paths("views", m.views);
    paths("themes", m.themes);
    // effects = "fx": the application's own effects, a directory.
    if (auto e = t["effects"].value<std::string>()) m.effects = m.base / std::filesystem::path(std::u8string(e->begin(), e->end()));
    // theme = ["dark", "compact"]: the composition to start with.
    if (const toml::array* a = t["theme"].as_array())
        for (const auto& v : *a)
            if (auto s = v.value<std::string>()) m.theme.push_back(*s);
    // [package.metadata.openmui.scripts]: name = "logic/main.ts", or
    // name = { lang = "ts", entry = "logic/main.ts" }; run in the names' order.
    if (const toml::table* scripts = t["scripts"].as_table())
        for (const auto& [name, v] : *scripts) {
            std::optional<std::string> lang, entry = v.value<std::string>();
            if (const toml::table* x = v.as_table()) {
                lang = (*x)["lang"].value<std::string>();
                entry = (*x)["entry"].value<std::string>();
            }
            if (!entry) return std::unexpected(std::format("{}: the script `{}` names its `entry`", utf8(file), name.str()));
            m.scripts.push_back({lang.value_or(""), std::filesystem::path(std::u8string(entry->begin(), entry->end()))});
        }
    return m;
}

}  // namespace omui
