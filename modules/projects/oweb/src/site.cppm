// oweb:site — pages into routes, a route into its writings (design 2026-11-26-oweb-site-framework-design.md §3).
//
// A site is a directory with `pages/`: every Markdown file there is a route (`pages/index.md` is `/`,
// `pages/guide/start.md` is `/guide/start/`, a directory's `index.md` the directory's own). A route is evaluated
// once, headless, into its semantic tree — the navigation the routes give and the page's document — and the tree is
// written twice: `index.html`, the whole page as HTML that reads with no script (omui.semantic's static_html), and
// `index.md`, the document as Markdown (omui.ui.markdown's markdown_of) for an agent that only fetches. Nothing is
// written that the tree does not hold, and the tree's writings lose nothing of it (0.1.0 plan UI1, UI2). Links
// between pages are written as each writing's own addresses: a route's directory in HTML, its `index.md` in
// Markdown. The routes are one table: the navigation, `routes.xdef.json`, `llms.txt` and `sitemap.xml` are made
// from it.
export module oweb:site;

import std;

export namespace oweb {

struct Heading {
    int         level = 1;
    std::string text;
    std::string anchor;
};

struct Route {
    std::string          path;      // where it is served: "/", "/guide/start/"
    std::string          source;    // its page, from the site's directory: "pages/guide/start.md"
    std::string          title;     // its first heading; its path where it has none
    std::string          language;  // "en", "zh"
    std::vector<Heading> headings;
};

struct SiteOptions {
    std::string              title;              // empty: the first route's
    std::vector<std::string> languages{"en"};    // the default first; a first directory of that name is that language's pages
    std::string              url;                // where the site is served; empty: no sitemap
};

// A route as it is written.
struct Page {
    Route       route;
    std::string html;      // the whole document: <!doctype html> …
    std::string markdown;  // its document as Markdown
    std::size_t nodes = 0; // of its semantic tree
};

struct File {
    std::string    path;  // from the directory written
    std::uintmax_t bytes = 0;
};

struct Built {
    std::vector<Route> routes;
    std::vector<File>  files;
};

// The routes a site's pages give, by address; or why not (no pages/, two pages of one address).
[[nodiscard]] std::expected<std::vector<Route>, std::string> collect(const std::filesystem::path& site, const SiteOptions& options = {});

// One route evaluated and written, the others its navigation.
[[nodiscard]] std::expected<Page, std::string> evaluate(const std::filesystem::path& site, std::span<const Route> routes, const Route& route, const SiteOptions& options = {});

// The site written to `out`: every route's two files, what pages/ holds beside its pages (pictures) at the same
// place, and the site's tables.
[[nodiscard]] std::expected<Built, std::string> build(const std::filesystem::path& site, const std::filesystem::path& out, const SiteOptions& options = {});

// The routes table as its snapshot (`routes@1`), the list for agents (llms.txt: each route's title and its Markdown's
// address), and the sitemap (each route's address under `url`).
[[nodiscard]] std::string routes_json(std::span<const Route> routes, const SiteOptions& options);
[[nodiscard]] std::string llms_txt(std::span<const Route> routes, const SiteOptions& options);
[[nodiscard]] std::string sitemap_xml(std::span<const Route> routes, const SiteOptions& options);

// The address that leads from one route to another, as a route's files say it: "../zh/", "guide/start/", "./".
[[nodiscard]] std::string relative(std::string_view from, std::string_view to);

}  // namespace oweb
