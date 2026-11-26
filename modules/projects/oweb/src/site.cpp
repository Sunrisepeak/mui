module oweb;

import std;
import mcpplibs.os;
import mlibs.delivery;
import mlibs.markdown;
import omui.core;
import omui.view;
import omui.semantic;
import omui.host.headless;
import omui.ui.base;
import omui.ui.markdown;
import :site;

namespace oweb {

namespace {

namespace md = mlibs::markdown;
using Node = omui::spec::SemanticNode;

std::string utf8(const std::filesystem::path& path) { return mcpplibs::os::fs::utf8(path); }
// With `/` between its parts on every system: an address, a path a table records.
std::string generic(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::expected<std::string, std::string> read(const std::filesystem::path& file) {
    auto text = mcpplibs::os::fs::read(file);
    if (!text) return std::unexpected(std::format("{}: {}", utf8(file), text.error().message()));
    return std::move(*text);
}

// Text and attribute values as HTML and XML read them.
std::string escaped(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// A page's address: pages/index.md is "/", pages/guide/start.md "/guide/start/", a directory's index.md its own.
std::string address_of(const std::filesystem::path& from_pages) {
    std::filesystem::path path = from_pages;
    path.replace_extension();
    if (path.filename() == "index") path = path.parent_path();
    const std::string inner = generic(path);
    return inner.empty() || inner == "." ? "/" : std::format("/{}/", inner);
}

// Where a link written in `source` (a page, from the site's directory) leads, when that is inside the site: the
// path from the site's directory, and what follows it (`#anchor`, `?query`).
std::optional<std::pair<std::string, std::string>> inside(std::string_view source, std::string_view href) {
    if (href.empty() || href.starts_with('#') || href.starts_with('/') || href.find("://") != std::string_view::npos || href.starts_with("mailto:") ||
        href.starts_with("data:"))
        return std::nullopt;
    const std::size_t cut = href.find_first_of("#?");
    const std::string_view path = href.substr(0, cut), rest = cut == std::string_view::npos ? std::string_view{} : href.substr(cut);
    const std::filesystem::path target = (mcpplibs::os::fs::from_utf8(source).parent_path() / mcpplibs::os::fs::from_utf8(path)).lexically_normal();
    const std::string normal = generic(target);
    if (normal.starts_with("..")) return std::nullopt;
    return std::pair{normal, std::string(rest)};
}

// The default look of a page read before any engine is there: one readable column in the reader's own fonts and
// colours, light or dark as their system is, and plain on paper. A site's theme replaces it (ST4); the engine's
// own look is the canvas's (UI5 holds the two together).
constexpr std::string_view sheet =
    "html{color-scheme:light dark}"
    "body{margin:0;font:16px/1.6 system-ui,-apple-system,\"Segoe UI\",\"Noto Sans\",\"PingFang SC\",\"Microsoft YaHei\",sans-serif}"
    "[data-omui-target=app]{max-width:46rem;margin:0 auto;padding:1.5rem 1.25rem 4rem}"
    "nav{display:flex;flex-wrap:wrap;gap:.25rem 1rem;padding-bottom:1rem;border-bottom:1px solid color-mix(in srgb,currentColor 18%,transparent)}"
    "pre{overflow:auto;padding:.75rem 1rem;border-radius:8px;background:color-mix(in srgb,currentColor 7%,transparent)}"
    "code{font:.9em ui-monospace,\"SF Mono\",Menlo,Consolas,monospace}"
    "table{border-collapse:collapse}th,td{padding:.3rem .75rem;border:1px solid color-mix(in srgb,currentColor 18%,transparent)}"
    "blockquote{margin-left:0;padding-left:1rem;border-left:3px solid color-mix(in srgb,currentColor 25%,transparent)}"
    "img{max-width:100%}"
    "@media print{nav{display:none}a{color:inherit;text-decoration:none}}";

struct Evaluated {
    std::vector<Node> nodes;  // the whole page's, pre-order
    std::size_t       document = 0, document_end = 0;  // the document's subtree in them
};

}  // namespace

std::string relative(std::string_view from, std::string_view to) {
    const auto parts = [](std::string_view path) {
        std::vector<std::string_view> out;
        for (const auto part : std::views::split(path, '/'))
            if (!std::ranges::empty(part)) out.emplace_back(part.begin(), part.end());
        return out;
    };
    const auto a = parts(from), b = parts(to);
    std::size_t same = 0;
    while (same < a.size() && same < b.size() && a[same] == b[same]) ++same;
    std::string out;
    for (std::size_t up = same; up < a.size(); ++up) out += "../";
    for (std::size_t down = same; down < b.size(); ++down) out += std::format("{}/", b[down]);
    return out.empty() ? "./" : out;
}

std::expected<std::vector<Route>, std::string> collect(const std::filesystem::path& site, const SiteOptions& options) {
    // What the navigation and the tables say of a page, read from its document: no page is drawn for that. Its
    // language is its address's first part where that is one of the site's, the site's first otherwise.
    const auto described = [&](Route& route) -> std::expected<void, std::string> {
        const auto text = read(site / mcpplibs::os::fs::from_utf8(route.source));
        if (!text) return std::unexpected(text.error());
        const md::Document document = md::parse(*text);
        md::walk(document.root, [&](const md::Block& block) {
            if (block.kind == md::Kind::heading) route.headings.push_back({block.level, md::plain_text(block), block.anchor});
        });
        if (route.title.empty()) route.title = route.headings.empty() ? route.path : route.headings.front().text;
        const std::size_t end = route.path.find('/', 1);
        const std::string first = end == std::string::npos ? std::string{} : route.path.substr(1, end - 1);
        const auto language = std::ranges::find(options.languages, first);
        route.language = language != options.languages.end() ? *language : options.languages.empty() ? std::string("en") : options.languages.front();
        return {};
    };
    std::vector<Route> routes;
    if (!options.routes.empty()) {
        // Stated: these pages, in this order.
        for (const Declared& declared : options.routes) {
            Route route;
            route.path = declared.path;
            route.source = declared.source;
            route.title = declared.title;
            std::error_code missing;
            if (!std::filesystem::is_regular_file(site / mcpplibs::os::fs::from_utf8(route.source), missing))
                return std::unexpected(std::format("the route `{}` states the page {}, which is not in {}: write it, or take the route out of oweb.config.ts", route.path, route.source, utf8(site)));
            if (std::ranges::contains(routes, route.path, &Route::path)) return std::unexpected(std::format("the route `{}` is stated twice: keep one", route.path));
            if (auto read_in = described(route); !read_in) return std::unexpected(read_in.error());
            routes.push_back(std::move(route));
        }
        return routes;
    }
    const std::filesystem::path pages = site / "pages";
    std::error_code error;
    if (!std::filesystem::is_directory(pages, error)) return std::unexpected(std::format("{} has no pages/: a site's pages are the Markdown files there", utf8(site)));
    for (const auto& entry : std::filesystem::recursive_directory_iterator(pages, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
        const std::filesystem::path from_pages = entry.path().lexically_relative(pages);
        Route route;
        route.path = address_of(from_pages);
        route.source = std::format("pages/{}", generic(from_pages));
        if (auto read_in = described(route); !read_in) return std::unexpected(read_in.error());
        routes.push_back(std::move(route));
    }
    if (error) return std::unexpected(std::format("{}: {}", utf8(pages), error.message()));
    std::ranges::sort(routes, {}, &Route::path);
    for (std::size_t at = 1; at < routes.size(); ++at)
        if (routes[at].path == routes[at - 1].path)
            return std::unexpected(std::format("{} and {} are one address ({}): keep one", routes[at - 1].source, routes[at].source, routes[at].path));
    if (routes.empty()) return std::unexpected(std::format("{} has no page: a page is a Markdown file", utf8(pages)));
    return routes;
}

std::expected<Page, std::string> evaluate(const std::filesystem::path& site, std::span<const Route> routes, const Route& route, const SiteOptions& options) {
    using namespace omui;
    const auto text = read(site / mcpplibs::os::fs::from_utf8(route.source));
    if (!text) return std::unexpected(text.error());

    // The page as an interface: the navigation the routes give (a site of one page has none) and its document.
    Headless host({.level = HeadlessLevel::layout, .size = {960, 1200}});
    std::vector<Element> parts;
    if (routes.size() > 1) {
        std::vector<Element> links;
        for (std::size_t index = 0; index < routes.size(); ++index) {
            // The site's own language first, then each language's pages together.
            if (routes[index].language != route.language) continue;
            links.push_back(link({.id = std::format("r{}", index), .href = relative(route.path, routes[index].path), .text = routes[index].title}));
        }
        for (std::size_t index = 0; index < routes.size(); ++index)
            if (routes[index].language != route.language && routes[index].path == std::format("/{}/", routes[index].language))
                links.push_back(link({.id = std::format("r{}", index), .href = relative(route.path, routes[index].path), .text = routes[index].title}));
        Element navigation = row({.id = "nav", .gap = 16}, std::move(links));
        navigation.prop("role", Source{Value("navigation"), {}, {}});
        navigation.prop("label", Source{Value(options.title.empty() ? std::string("Pages") : options.title), {}, {}});
        parts.push_back(std::move(navigation));
    }
    Element main = column({.id = "main"}, {markdown({.id = "doc", .origin = MarkdownOrigin::app, .source = *text})});
    main.prop("role", Source{Value("main"), {}, {}});
    parts.push_back(std::move(main));
    host.mount("page", column({.id = "page", .padding = 24, .gap = 16}, std::move(parts)));
    host.settle();

    semantic::Snapshot snapshot = host.tree().snapshot({});
    if (snapshot.nodes.empty()) return std::unexpected(std::format("{}: the page made no tree", route.source));
    // The document's subtree: its node and what follows it deeper.
    std::size_t document = snapshot.nodes.size(), document_end = snapshot.nodes.size();
    for (std::size_t at = 0; at < snapshot.nodes.size(); ++at)
        if (snapshot.nodes[at].role == Role::document) {
            document = at;
            const std::string under = snapshot.nodes[at].id + "/";
            document_end = at + 1;
            while (document_end < snapshot.nodes.size() && snapshot.nodes[document_end].id.starts_with(under)) ++document_end;
            break;
        }
    if (document == snapshot.nodes.size()) return std::unexpected(std::format("{}: the page has no document", route.source));

    // A link to another page is written as that writing's own address: the route's directory in HTML, its index.md in
    // Markdown. What stands beside the pages (a picture) is written where it stands in pages/ and found from the
    // route's directory. A link inside the site that leads nowhere — a page that is no route, an anchor its page has
    // not, a file that is not there — is a problem of the page, said once.
    std::vector<std::string> problems;
    const auto problem = [&](std::string text) {
        if (!std::ranges::contains(problems, text)) problems.push_back(std::move(text));
    };
    const auto rewritten = [&](std::string_view href, bool markdown) -> std::optional<std::string> {
        const auto target = inside(route.source, href);
        if (!target) return std::nullopt;
        const auto page = std::ranges::find(routes, target->first, &Route::source);
        if (page != routes.end()) {
            if (target->second.starts_with('#') && target->second.size() > 1) {
                const std::string_view anchor = std::string_view(target->second).substr(1);
                // A heading's anchor, or a place the page's own markup names (id="…").
                if (!std::ranges::contains(page->headings, anchor, &Heading::anchor)) {
                    const auto text = read(site / mcpplibs::os::fs::from_utf8(page->source));
                    if (!text || !text->contains(std::format("\"{}\"", anchor)))
                        problem(std::format("{} links to {}: {} has no heading of the anchor `{}`", route.source, href, page->source, anchor));
                }
            }
            return std::format("{}{}{}", relative(route.path, page->path), markdown ? "index.md" : "", target->second);
        }
        if (target->first.ends_with(".md")) {
            problem(std::format("{} links to {}, which is no page of the site ({})", route.source, href, target->first));
            return std::nullopt;
        }
        if (!target->first.starts_with("pages/")) return std::nullopt;
        std::error_code missing;
        if (!std::filesystem::is_regular_file(site / mcpplibs::os::fs::from_utf8(target->first), missing))
            problem(std::format("{} names {}, which is not there ({})", route.source, href, target->first));
        const std::filesystem::path beside = mcpplibs::os::fs::from_utf8(std::string_view(target->first).substr(6));
        const std::string directory = beside.has_parent_path() ? std::format("/{}/", generic(beside.parent_path())) : std::string("/");
        std::string to = relative(route.path, directory);
        if (to == "./") to.clear();
        return std::format("{}{}{}", to, generic(beside.filename()), target->second);
    };
    const auto addressed = [&](std::span<const Node> nodes, bool markdown) {
        std::vector<Node> out(nodes.begin(), nodes.end());
        for (Node& node : out)
            for (const std::string_view key : {"href", "source"})
                if (const Value* stated = node.properties.get(key); stated && stated->type() == Value::Type::text)
                    if (const auto address = rewritten(stated->as_string(), markdown)) node.properties.set(key, Value(*address));
        return out;
    };

    Page page;
    page.route = route;
    page.nodes = snapshot.nodes.size();
    // Its sections: a heading begins one; a block's own text is a line of the section it stands in. A node a text's
    // mark places (a link, a picture in a line) has its words in that text already.
    {
        std::unordered_set<std::string_view> placed;
        for (std::size_t at = document; at < document_end; ++at)
            for (const spec::Mark& mark : snapshot.nodes[at].marks)
                if (!mark.node.empty()) placed.insert(mark.node);
        std::vector<std::pair<int, std::string>> above;  // the headings the place is under: level, text
        page.sections.push_back({route.path, {}, route.title, {}, {}});
        for (std::size_t at = document + 1; at < document_end; ++at) {
            const Node& node = snapshot.nodes[at];
            if (node.role == Role::heading) {
                const Value* stated = node.properties.get("level");
                const int level = static_cast<int>(stated && stated->is_number() ? stated->as_int() : 2);
                while (!above.empty() && above.back().first >= level) above.pop_back();
                Section section{route.path, {}, node.name, {}, {}};
                if (const Value* anchor = node.properties.get("anchor"); anchor && anchor->type() == Value::Type::text) section.anchor = std::string(anchor->as_string());
                for (const auto& [_, text] : above) section.trail.push_back(text);
                above.emplace_back(level, node.name);
                // The page's first heading names the page's beginning: one section, not two.
                if (page.sections.size() == 1 && page.sections.front().text.empty() && level == 1) page.sections.front() = std::move(section);
                else page.sections.push_back(std::move(section));
                continue;
            }
            const bool worded = node.role == Role::paragraph || node.role == Role::code || node.role == Role::cell || node.role == Role::column_header ||
                                node.role == Role::row_header || node.role == Role::text || (node.role == Role::image && !placed.contains(node.id));
            if (!worded || node.name.empty() || node.properties.get("comment")) continue;
            std::string& text = page.sections.back().text;
            if (!text.empty()) text += '\n';
            text += node.name;
        }
    }
    const std::span<const Node> all = snapshot.nodes;
    page.markdown = markdown_of(addressed(all.subspan(document, document_end - document), true));
    // The navigation's links are addresses already; the document's are rewritten.
    semantic::Snapshot written = snapshot;
    {
        std::vector<Node> inner = addressed(all.subspan(document, document_end - document), false);
        std::ranges::move(inner, written.nodes.begin() + static_cast<std::ptrdiff_t>(document));
    }
    // What a view adds to operate its document (the button that copies a block of code) works only with the engine:
    // it is no part of the page as it reads without one.
    {
        const std::string under = written.nodes[document].id + "/";
        std::unordered_set<std::string> left_out;
        for (const Node& node : written.nodes)
            if (node.role == Role::button && node.id.starts_with(under)) left_out.insert(node.id);
        for (Node& node : written.nodes) std::erase_if(node.children, [&](const std::string& id) { return left_out.contains(id); });
        std::erase_if(written.nodes, [&](const Node& node) { return left_out.contains(node.id); });
    }
    const std::string body = semantic::static_html(written, {});
    page.html = std::format(
        "<!doctype html>\n<html lang=\"{}\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "<title>{}</title>\n<meta name=\"generator\" content=\"oweb\">\n<link rel=\"alternate\" type=\"text/markdown\" href=\"index.md\">\n<style>{}</style>\n"
        "</head>\n<body>\n{}</body>\n</html>\n",
        escaped(route.language), escaped(options.title.empty() || options.title == route.title ? route.title : std::format("{} · {}", route.title, options.title)), sheet,
        body);
    page.problems = std::move(problems);
    return page;
}

std::string routes_json(std::span<const Route> routes, const SiteOptions& options) {
    using omui::Value;
    Value::List list;
    for (const Route& route : routes) {
        Value::List headings;
        for (const Heading& heading : route.headings)
            headings.push_back(Value{}.set("level", Value(static_cast<std::int64_t>(heading.level))).set("text", heading.text).set("anchor", heading.anchor));
        list.push_back(Value{}
                           .set("path", route.path)
                           .set("source", route.source)
                           .set("title", route.title)
                           .set("language", route.language)
                           .set("markdown", std::format("{}index.md", route.path))
                           .set("headings", Value(std::move(headings))));
    }
    Value::List languages;
    for (const std::string& language : options.languages) languages.emplace_back(language);
    return Value{}
               .set("$format", "routes@1")
               .set("site", Value{}.set("title", options.title.empty() && !routes.empty() ? routes.front().title : options.title).set("languages", Value(std::move(languages))).set("url", options.url))
               .set("routes", Value(std::move(list)))
               .json() +
           "\n";
}

std::string llms_txt(std::span<const Route> routes, const SiteOptions& options) {
    std::string out = std::format("# {}\n\n", options.title.empty() && !routes.empty() ? routes.front().title : options.title);
    out += "Every page of this site is also plain Markdown: the address beside each title. The routes, with their headings: site.routes.xdef.json. Every section's text, to search: site.search.xdef.json.\n\n";
    const std::string base = options.url.empty() ? std::string() : options.url.ends_with('/') ? options.url.substr(0, options.url.size() - 1) : options.url;
    for (const Route& route : routes) out += std::format("- [{}]({}{}index.md)\n", route.title, base, options.url.empty() ? route.path.substr(1) : route.path);
    return out;
}

std::string sitemap_xml(std::span<const Route> routes, const SiteOptions& options) {
    const std::string base = options.url.ends_with('/') ? options.url.substr(0, options.url.size() - 1) : options.url;
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
    for (const Route& route : routes) out += std::format("  <url><loc>{}{}</loc></url>\n", escaped(base), escaped(route.path));
    return out + "</urlset>\n";
}

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

std::expected<std::vector<Section>, std::string> sections_of(const std::filesystem::path& site, const SiteOptions& options) {
    const auto routes = collect(site, options);
    if (!routes) return std::unexpected(routes.error());
    std::vector<Section> sections;
    for (const Route& route : *routes) {
        auto page = evaluate(site, *routes, route, options);
        if (!page) return std::unexpected(page.error());
        std::ranges::move(page->sections, std::back_inserter(sections));
    }
    return sections;
}

std::expected<Built, std::string> build(const std::filesystem::path& site, const std::filesystem::path& out, const SiteOptions& given) {
    auto routes = collect(site, given);
    if (!routes) return std::unexpected(routes.error());
    SiteOptions options = given;
    if (options.title.empty()) options.title = routes->front().title;
    Built built;
    std::vector<Section> sections;
    std::vector<mlibs::delivery::File> files;
    const auto write = [&](const std::string& path, std::string_view bytes) -> std::expected<void, std::string> {
        const std::filesystem::path file = out / mcpplibs::os::fs::from_utf8(path);
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        if (auto written = mcpplibs::os::fs::write_atomic(file, bytes); !written) return std::unexpected(std::format("{}: {}", utf8(file), written.error().message()));
        files.push_back(mlibs::delivery::measured(path, bytes));
        return {};
    };
    // Every route evaluated before anything is written: a site with a link that leads nowhere is not built.
    std::vector<Page> pages;
    std::string problems;
    std::size_t found = 0;
    for (const Route& route : *routes) {
        auto page = evaluate(site, *routes, route, options);
        if (!page) return std::unexpected(page.error());
        for (const std::string& problem : page->problems)
            if (++found <= 12) problems += std::format("\n  {}", problem);
        pages.push_back(std::move(*page));
    }
    if (found > 0)
        return std::unexpected(std::format("{} link{} inside the site lead{} nowhere; nothing was written:{}{}", found, found == 1 ? "" : "s", found == 1 ? "s" : "", problems,
                                           found > 12 ? std::format("\n  … and {} more", found - 12) : std::string{}));
    for (Page& page : pages) {
        const std::string directory = page.route.path.substr(1);
        if (auto written = write(directory + "index.html", page.html); !written) return std::unexpected(written.error());
        if (auto written = write(directory + "index.md", page.markdown); !written) return std::unexpected(written.error());
        std::ranges::move(page.sections, std::back_inserter(sections));
    }
    // What pages/ holds beside its pages (pictures, files to download), at the same place.
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(site / "pages", error)) {
        if (!entry.is_regular_file() || entry.path().extension() == ".md") continue;
        const auto bytes = read(entry.path());
        if (!bytes) return std::unexpected(bytes.error());
        if (auto written = write(generic(entry.path().lexically_relative(site / "pages")), *bytes); !written) return std::unexpected(written.error());
    }
    if (auto written = write("site.routes.xdef.json", routes_json(*routes, options)); !written) return std::unexpected(written.error());
    if (auto written = write("llms.txt", llms_txt(*routes, options)); !written) return std::unexpected(written.error());
    if (auto written = write("site.search.xdef.json", search_json(sections)); !written) return std::unexpected(written.error());
    if (!options.url.empty())
        if (auto written = write("sitemap.xml", sitemap_xml(*routes, options)); !written) return std::unexpected(written.error());
    // GitHub Pages serves the directory as it is (no Jekyll pass over it).
    if (auto written = write(".nojekyll", ""); !written) return std::unexpected(written.error());
    std::vector<mlibs::delivery::Budget> budgets;
    if (options.readable > 0) budgets.push_back({"a route readable", "**/index.html", mlibs::delivery::Measure::sent, mlibs::delivery::Over::each, options.readable});
    built.delivery = mlibs::delivery::held(std::move(files), budgets);
    // The manifest is of the site, not of itself: written last and not listed.
    {
        const std::filesystem::path file = out / "site.delivery.xdef.json";
        if (auto written = mcpplibs::os::fs::write_atomic(file, mlibs::delivery::to_json(built.delivery)); !written)
            return std::unexpected(std::format("{}: {}", utf8(file), written.error().message()));
    }
    built.routes = std::move(*routes);
    return built;
}

}  // namespace oweb
