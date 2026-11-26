// oweb:site — pages into routes, a route into its writings (design 2026-11-26-oweb-site-framework-design.md §3).
//
// A site is a directory with `pages/`: every Markdown file there is a route (`pages/index.md` is `/`,
// `pages/guide/start.md` is `/guide/start/`, a directory's `index.md` the directory's own). A route is evaluated
// once, headless, into its semantic tree — the navigation the routes give and the page's document — and the tree is
// written twice: `index.html`, the whole page as HTML that reads with no script (omui.semantic's static_html), and
// `index.md`, the document as Markdown (omui.ui.markdown's markdown_of) for an agent that only fetches. Nothing is
// written that the tree does not hold, and the tree's writings lose nothing of it (0.1.0 plan UI1, UI2). Links
// between pages are written as each writing's own addresses: a route's directory in HTML, its `index.md` in
// Markdown. The routes are one table: the navigation, `site.routes.xdef.json`, `llms.txt` and `sitemap.xml` are made
// from it.
export module oweb:site;

import std;
export import mlibs.delivery;

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

// A route a site's configuration states: where it is served, its page, and what the navigation calls it.
struct Declared {
    std::string path;    // "/start/"
    std::string source;  // "pages/guide/start.md"
    std::string title;   // empty: its page's first heading
};

struct SiteOptions {
    std::string              title;              // empty: the first route's
    std::vector<std::string> languages{"en"};    // the default first; a first directory of that name is that language's pages
    std::string              url;                // where the site is served; empty: no sitemap
    // The most a route sends before it reads: its index.html as a static host sends it (gzip) — the page carries its
    // style and needs nothing else to read. 100 KB is the plan's assumed budget (0.1.0 plan §6); 0 holds none.
    std::uintmax_t           readable = 100'000;
    // Stated routes: these pages and no others, in this order — the navigation's. Empty: every page of pages/, by
    // address.
    std::vector<Declared>    routes;
};

// A part of a page a reader can be sent to: a heading and what stands under it up to the next heading. What a
// person's search box and an agent's query both answer with (one index, made from the tree: 0.1.0 plan ST5).
struct Section {
    std::string              route;   // "/guide/start/"
    std::string              anchor;  // the heading's; empty: the page's beginning
    std::string              title;   // the heading's text; the page's title for what stands before any
    std::vector<std::string> trail;   // the headings it is under, the page's first
    std::string              text;    // what it holds, a line a block
};

// A route as it is written.
struct Page {
    Route                route;
    std::string          html;      // the whole document: <!doctype html> …
    std::string          markdown;  // its document as Markdown
    std::size_t          nodes = 0; // of its semantic tree
    std::vector<Section> sections;
    // Links inside the site that lead nowhere: a page that is no route, an anchor its page has not, a file that is
    // not there. A build with any writes nothing.
    std::vector<std::string> problems;
};

// A section a query found: how well, and the line of it that says so.
struct Hit {
    Section     section;
    double      score = 0;
    std::string excerpt;
};

struct Built {
    std::vector<Route>        routes;
    // Every file written, as stored and as a static host sends it, and the budgets' verdicts (mlibs.delivery): what
    // the site's site.delivery.xdef.json says. A site over a budget is written all the same — the verdict says where.
    mlibs::delivery::Manifest delivery;
};

// The routes a site's pages give, by address — or the routes it states, in their order; or why not (no pages/, two
// pages of one address, a stated page that is not there).
[[nodiscard]] std::expected<std::vector<Route>, std::string> collect(const std::filesystem::path& site, const SiteOptions& options = {});

// One route evaluated and written, the others its navigation.
[[nodiscard]] std::expected<Page, std::string> evaluate(const std::filesystem::path& site, std::span<const Route> routes, const Route& route, const SiteOptions& options = {});

// The page a static host answers a lost address with (`404.html`): the site's own `pages/404.md`, or one that says
// so and leads home. It is served at any address, so its links are from the site's own address — the path of `url`,
// said by the document (`<base>`) — and it is no route: no table lists it.
[[nodiscard]] std::expected<Page, std::string> lost_page(const std::filesystem::path& site, std::span<const Route> routes, const SiteOptions& options = {});

// The site written to `out`: every route's two files, what pages/ holds beside its pages (pictures) at the same
// place, the site's tables, `404.html` and `.nojekyll` (GitHub Pages serves the directory as it is) and, last, the delivery
// manifest of all of it (site.delivery.xdef.json).
[[nodiscard]] std::expected<Built, std::string> build(const std::filesystem::path& site, const std::filesystem::path& out, const SiteOptions& options = {});

// The routes table as its snapshot (`routes@1`), the list for agents (llms.txt: each route's title and its Markdown's
// address), and the sitemap (each route's address under `url`).
[[nodiscard]] std::string routes_json(std::span<const Route> routes, const SiteOptions& options);
[[nodiscard]] std::string llms_txt(std::span<const Route> routes, const SiteOptions& options);
[[nodiscard]] std::string sitemap_xml(std::span<const Route> routes, const SiteOptions& options);

// The index every route's sections make (`search@1`): what a built site serves as site.search.xdef.json.
[[nodiscard]] std::string search_json(std::span<const Section> sections);

// The sections that hold every word of the query, best first: a word of Latin letters and digits matches whole or
// as the start of a word, without case; text without spaces between words (Chinese, Japanese) matches by its
// characters in a row. A word in a title or a trail counts more than one in the text.
[[nodiscard]] std::vector<Hit> search(std::span<const Section> sections, std::string_view query, std::size_t limit = 8);

// Every route's sections, each route evaluated: what `docs.search` asks without building the site.
[[nodiscard]] std::expected<std::vector<Section>, std::string> sections_of(const std::filesystem::path& site, const SiteOptions& options = {});

// The address that leads from one route to another, as a route's files say it: "../zh/", "guide/start/", "./".
[[nodiscard]] std::string relative(std::string_view from, std::string_view to);

}  // namespace oweb
