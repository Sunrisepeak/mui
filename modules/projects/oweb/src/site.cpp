module oweb;

import std;
import mcpplibs.os;
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
    const std::filesystem::path pages = site / "pages";
    std::error_code error;
    if (!std::filesystem::is_directory(pages, error)) return std::unexpected(std::format("{} has no pages/: a site's pages are the Markdown files there", utf8(site)));
    std::vector<Route> routes;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(pages, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
        const std::filesystem::path from_pages = entry.path().lexically_relative(pages);
        Route route;
        route.path = address_of(from_pages);
        route.source = std::format("pages/{}", generic(from_pages));
        const auto text = read(entry.path());
        if (!text) return std::unexpected(text.error());
        // What the navigation and the tables say of it, read from its document: no page is drawn for that.
        const md::Document document = md::parse(*text);
        md::walk(document.root, [&](const md::Block& block) {
            if (block.kind == md::Kind::heading) route.headings.push_back({block.level, md::plain_text(block), block.anchor});
        });
        route.title = route.headings.empty() ? route.path : route.headings.front().text;
        const std::string first = from_pages.begin() == from_pages.end() ? std::string{} : generic(*from_pages.begin());
        const auto language = std::ranges::find(options.languages, first);
        route.language = language != options.languages.end() && from_pages.has_parent_path() ? *language : options.languages.empty() ? std::string("en") : options.languages.front();
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
    // Markdown; what stands beside the pages (a picture) is found from the route's directory, one deeper than its page.
    const bool own_directory = route.source.ends_with("/index.md");
    const auto rewritten = [&](std::string_view href, bool markdown) -> std::optional<std::string> {
        const auto target = inside(route.source, href);
        if (!target) return std::nullopt;
        const auto page = std::ranges::find(routes, target->first, &Route::source);
        if (page != routes.end()) return std::format("{}{}{}", relative(route.path, page->path), markdown ? "index.md" : "", target->second);
        return own_directory ? std::nullopt : std::optional(std::format("../{}", href));
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
    out += "Every page of this site is also plain Markdown: the address beside each title. The routes, with their headings: routes.xdef.json.\n\n";
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

std::expected<Built, std::string> build(const std::filesystem::path& site, const std::filesystem::path& out, const SiteOptions& given) {
    auto routes = collect(site, given);
    if (!routes) return std::unexpected(routes.error());
    SiteOptions options = given;
    if (options.title.empty()) options.title = routes->front().title;
    Built built;
    const auto write = [&](const std::string& path, std::string_view bytes) -> std::expected<void, std::string> {
        const std::filesystem::path file = out / mcpplibs::os::fs::from_utf8(path);
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        if (auto written = mcpplibs::os::fs::write_atomic(file, bytes); !written) return std::unexpected(std::format("{}: {}", utf8(file), written.error().message()));
        built.files.push_back({path, bytes.size()});
        return {};
    };
    for (const Route& route : *routes) {
        auto page = evaluate(site, *routes, route, options);
        if (!page) return std::unexpected(page.error());
        const std::string directory = route.path.substr(1);
        if (auto written = write(directory + "index.html", page->html); !written) return std::unexpected(written.error());
        if (auto written = write(directory + "index.md", page->markdown); !written) return std::unexpected(written.error());
    }
    // What pages/ holds beside its pages (pictures, files to download), at the same place.
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(site / "pages", error)) {
        if (!entry.is_regular_file() || entry.path().extension() == ".md") continue;
        const auto bytes = read(entry.path());
        if (!bytes) return std::unexpected(bytes.error());
        if (auto written = write(generic(entry.path().lexically_relative(site / "pages")), *bytes); !written) return std::unexpected(written.error());
    }
    if (auto written = write("routes.xdef.json", routes_json(*routes, options)); !written) return std::unexpected(written.error());
    if (auto written = write("llms.txt", llms_txt(*routes, options)); !written) return std::unexpected(written.error());
    if (!options.url.empty())
        if (auto written = write("sitemap.xml", sitemap_xml(*routes, options)); !written) return std::unexpected(written.error());
    built.routes = std::move(*routes);
    return built;
}

}  // namespace oweb
