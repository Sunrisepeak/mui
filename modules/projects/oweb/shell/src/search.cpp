module oweb.shell;

import std;
import mlibs.json;
import omui.core;

// The site's index and its query (0.1.0 plan ST5): one index made from every page's tree, one function that answers
// a query over it — the builder's command (`docs.search`) and the page's own search box call the same one, so a
// person and an agent asking for the same words are answered with the same sections.
namespace oweb::shell {

std::string search_json(std::span<const Section> sections) {
    using omui::Value;
    Value::List list;
    for (const Section& section : sections) {
        Value::List trail;
        for (const std::string& above : section.trail) trail.emplace_back(above);
        list.push_back(Value{}.set("route", section.route).set("anchor", section.anchor).set("title", section.title).set("trail", Value(std::move(trail))).set("text", section.text));
    }
    return Value{}.set("$format", "search@1").set("sections", Value(std::move(list))).json() + "\n";
}

namespace {

// A query's and a text's words: runs of letters and digits, lower case; each character outside ASCII a word of its
// own beside its neighbour (two in a row), so text written without spaces matches by characters in a row.
struct Words {
    std::vector<std::string> latin;   // "install", "omui"
    std::vector<std::string> joined;  // "安装", "装第"
};

Words words_of(std::string_view text) {
    Words out;
    std::string run;
    std::string previous;  // the character outside ASCII just before
    const auto end_run = [&] {
        if (!run.empty()) out.latin.push_back(std::move(run));
        run.clear();
    };
    for (std::size_t at = 0; at < text.size();) {
        const auto byte = static_cast<unsigned char>(text[at]);
        if (byte < 0x80) {
            previous.clear();
            if (std::isalnum(byte)) run += static_cast<char>(std::tolower(byte));
            else end_run();
            ++at;
            continue;
        }
        end_run();
        std::size_t length = byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : 2;
        length = std::min(length, text.size() - at);
        const std::string character(text.substr(at, length));
        if (!previous.empty()) out.joined.push_back(previous + character);
        previous = character;
        at += length;
    }
    end_run();
    return out;
}

// How often a word of the query is in a text's words: whole, or as the beginning of one.
int count_of(const Words& text, std::string_view word, bool joined) {
    int count = 0;
    for (const std::string& one : joined ? text.joined : text.latin) count += joined ? one == word : one.starts_with(word);
    return count;
}

}  // namespace

std::vector<Hit> search(std::span<const Section> sections, std::string_view query, std::size_t limit) {
    const Words asked = words_of(query);
    // One character outside ASCII alone is a word too ("树").
    std::vector<std::string> single;
    if (asked.joined.empty() && asked.latin.empty()) {
        for (std::size_t at = 0; at < query.size();) {
            const auto byte = static_cast<unsigned char>(query[at]);
            const std::size_t length = std::min<std::size_t>(byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0x80 ? 2 : 1, query.size() - at);
            if (byte >= 0x80) single.emplace_back(query.substr(at, length));
            at += length;
        }
    }
    std::vector<Hit> hits;
    if (asked.latin.empty() && asked.joined.empty() && single.empty()) return hits;
    for (const Section& section : sections) {
        std::string trail;
        for (const std::string& above : section.trail) trail += above + "\n";
        const Words title = words_of(section.title), over = words_of(trail), body = words_of(section.text);
        double score = 0;
        bool all = true;
        const auto weigh = [&](std::string_view word, bool joined) {
            const int in_title = count_of(title, word, joined), in_trail = count_of(over, word, joined), in_body = count_of(body, word, joined);
            if (in_title + in_trail + in_body == 0) all = false;
            score += 3.0 * in_title + 1.5 * in_trail + std::min(in_body, 5);
        };
        for (const std::string& word : asked.latin) weigh(word, false);
        for (const std::string& word : asked.joined) weigh(word, true);
        for (const std::string& character : single)
            if (section.title.contains(character)) score += 3;
            else if (section.text.contains(character)) score += 1;
            else all = false;
        if (!all || score <= 0) continue;
        // The line that says so: the first of its text holding a word asked for, else its beginning.
        std::string excerpt;
        for (const auto line : std::views::split(std::string_view(section.text), '\n')) {
            const std::string_view one(line.begin(), line.end());
            const Words in_line = words_of(one);
            const bool holds = std::ranges::any_of(asked.latin, [&](const std::string& word) { return count_of(in_line, word, false) > 0; }) ||
                               std::ranges::any_of(asked.joined, [&](const std::string& word) { return count_of(in_line, word, true) > 0; }) ||
                               std::ranges::any_of(single, [&](const std::string& character) { return one.contains(character); });
            if (excerpt.empty() || holds) excerpt = std::string(one);
            if (holds) break;
        }
        if (excerpt.size() > 240) {
            std::size_t cut = 240;
            while (cut > 0 && (static_cast<unsigned char>(excerpt[cut]) & 0xC0) == 0x80) --cut;  // at a character's start
            excerpt = excerpt.substr(0, cut) + "…";
        }
        hits.push_back({section, score, std::move(excerpt)});
    }
    std::ranges::stable_sort(hits, [](const Hit& a, const Hit& b) { return a.score > b.score; });
    if (hits.size() > limit) hits.resize(limit);
    return hits;
}

std::expected<std::vector<Section>, std::string> sections_from(std::string_view index) {
    const auto parsed = mlibs::json::Document::parse(index, "the site's index");
    if (!parsed) return std::unexpected(parsed.error().text());
    if (parsed->root()["$format"].text().value_or({}) != "search@1") return std::unexpected(std::string("the site's index is no search@1"));
    std::vector<Section> sections;
    for (const mlibs::json::Node& one : parsed->root()["sections"].items()) {
        Section section{one["route"].text().value_or({}), one["anchor"].text().value_or({}), one["title"].text().value_or({}), {}, one["text"].text().value_or({})};
        for (const mlibs::json::Node& above : one["trail"].items()) section.trail.push_back(above.text().value_or({}));
        sections.push_back(std::move(section));
    }
    return sections;
}

}  // namespace oweb::shell
