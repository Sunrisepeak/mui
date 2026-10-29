// Reading *.subtitles.omui.toml. toml++ reports a malformed file by
// throwing; this unit — the film's adapter to it — turns that into a
// problem at its line and column, as it does every other.
module openmui.showcase.promo_video;

import std;
import tomlplusplus;

namespace omui::promo {

std::expected<Subtitles, std::vector<Problem>> read_subtitles(std::string_view text, std::string_view file) {
    toml::table root;
    try {
        root = toml::parse(text, file);
    } catch (const toml::parse_error& e) {
        const auto s = e.source();
        return std::unexpected(std::vector<Problem>{{std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::string(e.description())}});
    }
    std::vector<Problem> out;
    auto at = [&](const toml::node& n, std::string message) {
        const auto s = n.source();
        out.push_back({std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::move(message)});
    };
    Subtitles subs;
    for (const auto& [k, v] : root) {
        if (k.str() == "doc") {
            if (const auto* d = v.as_string()) subs.doc = d->get();
            else at(v, "`doc` is text");
        } else if (k.str() != "line") {
            at(v, std::format("`{}` is not a key of a subtitles file (`doc`, `line`)", k.str()));
        }
    }
    if (const toml::table* lines = root["line"].as_table()) {
        for (const auto& [id, node] : *lines) {
            const toml::table* t = node.as_table();
            if (!t) {
                at(node, std::format("line `{}` is a table of `zh` and `en`", id.str()));
                continue;
            }
            Line l;
            l.id = std::string(id.str());
            for (const auto& [k, v] : *t) {
                const auto* s = v.as_string();
                if (k.str() != "zh" && k.str() != "en") at(v, std::format("line `{}`: `{}` is not a language here (`zh`, `en`)", l.id, k.str()));
                else if (!s) at(v, std::format("line `{}`: `{}` is text", l.id, k.str()));
                else (k.str() == "zh" ? l.zh : l.en) = s->get();
            }
            if (l.zh.empty() || l.en.empty()) at(node, std::format("line `{}` is in both languages: `zh` and `en`", l.id));
            subs.lines.push_back(std::move(l));
        }
    }
    if (!out.empty()) return std::unexpected(std::move(out));
    return subs;
}

}  // namespace omui::promo
