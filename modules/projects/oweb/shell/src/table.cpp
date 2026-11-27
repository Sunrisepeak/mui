module oweb.shell;

import std;
import mlibs.json;
import omui.core;

// A site's routes table read back, and the site described from it (0.1.0 plan AG5): what the site's doors answer
// with first — a build's llms.txt, the command's and a page's `site.describe` — is one text, written here.
namespace oweb::shell {

std::expected<Table, std::string> table_from(std::string_view routes_table) {
    const auto parsed = mlibs::json::Document::parse(routes_table, "the site's routes table");
    if (!parsed) return std::unexpected(parsed.error().text());
    if (parsed->root()["$format"].text().value_or({}) != "routes@1") return std::unexpected(std::string("the site's routes table is no routes@1"));
    const mlibs::json::Node site = parsed->root()["site"];
    Table table{site["title"].text().value_or({}), site["url"].text().value_or({}), {}, {}};
    for (const mlibs::json::Node& language : site["languages"].items()) table.languages.push_back(language.text().value_or({}));
    for (const mlibs::json::Node& one : parsed->root()["routes"].items()) {
        Entry entry{one["path"].text().value_or({}), one["source"].text().value_or({}), one["title"].text().value_or({}), one["language"].text().value_or({}), {}};
        for (const mlibs::json::Node& heading : one["headings"].items())
            entry.headings.push_back({static_cast<int>(heading["level"].number().value_or(1)), heading["text"].text().value_or({}), heading["anchor"].text().value_or({})});
        table.routes.push_back(std::move(entry));
    }
    return table;
}

std::string described(const Table& table) {
    std::string out = std::format("# {}\n\n", table.title);
    out += "Every page of this site is also plain Markdown: the address beside each title. The routes, with their headings: site.routes.xdef.json. Every section's text, to search: site.search.xdef.json.\n\n";
    const std::string base = table.url.empty() ? std::string() : table.url.ends_with('/') ? table.url.substr(0, table.url.size() - 1) : table.url;
    for (const Entry& route : table.routes) out += std::format("- [{}]({}{}index.md)\n", route.title, base, table.url.empty() ? route.path.substr(1) : route.path);
    return out;
}

namespace {

using omui::Value;

Value entry_value(const Entry& route) {
    Value::List headings;
    for (const Heading& heading : route.headings)
        headings.push_back(Value{}.set("level", Value(static_cast<std::int64_t>(heading.level))).set("text", heading.text).set("anchor", heading.anchor));
    return Value{}.set("path", route.path).set("source", route.source).set("title", route.title).set("language", route.language).set("headings", Value(std::move(headings)));
}

}  // namespace

std::string entry_json(const Entry& route) { return entry_value(route).json(); }

std::string routes_answer(const Table& table) {
    Value::List routes;
    for (const Entry& route : table.routes) routes.push_back(entry_value(route));
    return Value{}.set("ok", true).set("routes", Value(std::move(routes))).json();
}

std::string describe_answer(const Table& table, bool built, std::string_view next) {
    Value::List languages, routes;
    for (const std::string& language : table.languages) languages.emplace_back(language);
    for (const Entry& route : table.routes)
        routes.push_back(Value{}.set("path", route.path).set("title", route.title).set("language", route.language).set("markdown", std::format("{}index.md", route.path)));
    Value data = Value{}
                     .set("ok", true)
                     .set("title", table.title)
                     .set("url", table.url)
                     .set("languages", Value(std::move(languages)))
                     .set("routes", Value(std::move(routes)))
                     .set("built", built)
                     .set("tables", Value{}.set("routes", "site.routes.xdef.json").set("search", "site.search.xdef.json"));
    if (auto asked = omui::parse_json(next); asked && asked->list()) data.set("next", std::move(*asked));
    return data.set("text", described(table)).json();
}

std::string search_answer(std::span<const Section> sections, std::string_view query, std::size_t limit) {
    Value::List found;
    for (const Hit& hit : search(sections, query, limit)) {
        Value::List trail;
        for (const std::string& above : hit.section.trail) trail.emplace_back(above);
        found.push_back(Value{}
                            .set("route", hit.section.route)
                            .set("anchor", hit.section.anchor)
                            .set("title", hit.section.title)
                            .set("trail", Value(std::move(trail)))
                            .set("excerpt", hit.excerpt)
                            .set("markdown", std::format("{}index.md", hit.section.route))
                            .set("score", Value(hit.score)));
    }
    return Value{}.set("ok", true).set("query", query).set("sections", Value(static_cast<std::int64_t>(sections.size()))).set("found", Value(std::move(found))).json();
}

std::string page_answer(const Entry& route, std::string_view as, std::string_view text) {
    return Value{}.set("ok", true).set("route", entry_value(route)).set("as", as).set("text", text).json();
}

}  // namespace oweb::shell
