module oweb.shell;

import std;
import mlibs.json;

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

}  // namespace oweb::shell
