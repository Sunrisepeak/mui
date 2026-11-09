// Reading subtitles: the JSON a `*.subtitles.omui.ts` evaluates to (D-139),
// every problem at its JSON pointer and its line and column.
module omui.showcase.promo_video;

import std;
import mlibs.json;

namespace omui::promo {

std::expected<Subtitles, std::vector<Problem>> read_subtitles(std::string_view text, std::string_view file) {
    auto doc = mlibs::json::Document::parse(text, std::string(file));
    if (!doc) return std::unexpected(std::vector<Problem>{{doc.error().file, static_cast<int>(doc.error().line), static_cast<int>(doc.error().column), doc.error().message}});
    std::vector<Problem> out;
    auto at = [&](const mlibs::json::Node& n, std::string message) {
        const auto p = doc->problem(n, std::move(message));
        out.push_back({p.file, static_cast<int>(p.line), static_cast<int>(p.column), std::format("{}: {}", p.at, p.message)});
    };
    const auto root = doc->root()["subtitles"];
    for (const auto& [key, value] : doc->root().members())
        if (key != "subtitles") at(value, std::format("`{}` is not a key of a subtitles file: {{\"subtitles\": {{doc, lines}}}}", key));
    Subtitles subs;
    for (const auto& [key, value] : root.members()) {
        if (key == "doc") {
            if (auto d = value.text()) subs.doc = *d;
            else at(value, "`doc` is text");
        } else if (key != "lines") {
            at(value, std::format("`{}` is not a key of the subtitles (`doc`, `lines`)", key));
        }
    }
    // By id, as they have always been found.
    for (const auto& [id, node] : root["lines"].members_by_key()) {
        if (!node.is_object()) {
            at(node, std::format("line `{}` is an object of `zh` and `en`", id));
            continue;
        }
        Line l;
        l.id = id;
        for (const auto& [language, value] : node.members()) {
            const auto s = value.text();
            if (language != "zh" && language != "en") at(value, std::format("line `{}`: `{}` is not a language here (`zh`, `en`)", l.id, language));
            else if (!s) at(value, std::format("line `{}`: `{}` is text", l.id, language));
            else (language == "zh" ? l.zh : l.en) = *s;
        }
        if (l.zh.empty() || l.en.empty()) at(node, std::format("line `{}` is in both languages: `zh` and `en`", l.id));
        subs.lines.push_back(std::move(l));
    }
    if (!out.empty()) return std::unexpected(std::move(out));
    return subs;
}

}  // namespace omui::promo
