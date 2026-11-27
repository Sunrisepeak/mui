// oweb:site — pages into routes, a route into its writings (design 2026-11-26-oweb-site-framework-design.md §3).
//
// A site is a directory with `pages/`: every Markdown file there is a route (`pages/index.md` is `/`,
// `pages/guide/start.md` is `/guide/start/`, a directory's `index.md` the directory's own). A route is evaluated
// once, headless, into its semantic tree — the navigation the routes give and the page's document — and the tree is
// written twice: `index.html`, the whole page as HTML that reads with no script (omui.semantic's static_html), and
// `index.md`, the document as Markdown (omui.ui.markdown's markdown_of) for an agent that only fetches. Nothing is
// written that the tree does not hold, and the tree's writings lose nothing of it (0.1.0 plan UI1, UI2). Links
// between pages are written as each writing's own addresses: a route's directory in HTML, its `index.md` in
// Markdown; a link that begins with `/` is the site's own address (a route's, or a file's beside the pages), written
// the same way. The routes are one table: the navigation, `site.routes.xdef.json`, `llms.txt` and `sitemap.xml` are made
// from it.
export module oweb:site;

import std;
export import mlibs.delivery;
export import oweb.shell;

export namespace oweb {

// A page's heading and a site's table as it is read back: the shell's (a page's own program reads the same table).
using shell::Heading;
using shell::Table;

// The catalog of the command that builds the site, by name (a route's `reference: "oweb"`): carried with the
// builder, so a site anywhere — a project made from a template — can show how it is built.
inline constexpr std::string_view own_catalog = "oweb";

struct Route {
    std::string          path;      // where it is served: "/", "/guide/start/"
    std::string          source;    // its page, from the site's directory: "pages/guide/start.md"
    // The operation catalog its page is the reference of, from the site's directory ("../tool/tool.ops.xdef.ts"),
    // or `own_catalog`: the page is generated from it at each build. Empty: its page is `source`.
    std::string          reference;
    // The built page of an application it adopts (`omui build --web`: a directory with the page and its files), from
    // the site's directory or absolute: the page is served as it was built, its files beside it, and the site's
    // tables say of it what its own document says. `source` is then that page (`<page>/index.html`).
    std::string          page;
    std::string          title;     // its first heading (an application's page: its own title); its path where it has none
    std::string          language;  // "en", "zh"
    std::vector<Heading> headings;
    // When its page takes the site's program (D-260): "eager" at once, "idle" once the page has loaded and is idle,
    // "interaction" when the reader first operates something only a program answers — reading never takes it.
    // The route's own, or the site's. Nothing is taken by a site that has no program (`SiteOptions::engine`).
    std::string          load;
};

// A route a site's configuration states: where it is served, its page, and what the navigation calls it.
struct Declared {
    std::string path;    // "/start/"
    std::string source;  // "pages/guide/start.md"; empty where it is a reference
    std::string title;   // empty: its page's first heading
    std::string reference;  // an operation catalog (<product>.ops.xdef.ts, or its snapshot): the page is its reference
    std::string page;    // an application's built page (its directory): the route is that page, as built
    std::string load;    // when its page takes the program; empty: as the site says
};

struct SiteOptions {
    std::string              title;              // empty: the first route's
    std::vector<std::string> languages{"en"};    // the default first; a first directory of that name is that language's pages
    std::string              url;                // where the site is served; empty: no sitemap
    // The most a route sends before it reads: its index.html as a static host sends it (gzip) — the page carries its
    // style and needs nothing else to read. 100 KB is the plan's assumed budget (0.1.0 plan §6); 0 holds none.
    std::uintmax_t           readable = 100'000;
    // The most the site's program weighs as it is sent — its launcher and its code, fetched once for every page
    // (`engine/*`). 0: none stated; its weight is counted in the delivery manifest all the same, beside the faces'.
    std::uintmax_t           program = 0;
    // Stated routes: these pages and no others, in this order — the navigation's. Empty: every page of pages/, by
    // address.
    std::vector<Declared>    routes;
    // Each page written as its frame looks (omui.semantic's look, 0.1.0 plan UI5): the view's layout, the faces and
    // colours it drew — light and dark — in place of the default sheet, the faces' files beside the pages
    // (`assets/`), each cut to the characters the site's pages show. What the canvas shows when the engine is
    // there is then what the page showed before it.
    bool                     look = false;
    // The program its pages load in a browser (0.1.0 plan ST2): a world's directory — `omui build --web` of
    // oweb.reader, or a program of one's own packed as a world (omui-gen dist-toy --world) — from the site's
    // directory, or absolute. Its files are served once, from `engine/`; every page is made from the world's page
    // and is still the document it was, read with no script, until it takes the program — which reads the page's
    // own HTML back and draws it (D-284). The pages are written with their look, in a view of the window's size that
    // scrolls (a canvas is not scrolled by its window). Empty: pages that load nothing.
    std::string              engine;
    // When a page takes the program, where its route does not say: "eager", "idle", "interaction" (D-260).
    std::string              load = "eager";
    // The view of a page's parts (oweb.shell): the shell's own page where none is given. A site with a navigation of
    // its own gives its view here — as its builder (a program of its own: `oweb::Sites{view}` behind the same
    // entries) — and the same one to the program its pages load, which reads the parts back from the page whatever
    // the view placed them in. Such a site states `look` or an `engine`: the default sheet knows the shell's own
    // page only.
    shell::View              view;
};

// A part of a page a reader can be sent to, and a section a query found: the shell's (oweb.shell — the page's own
// search box answers with the same function).
using shell::Hit;
using shell::Section;

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
    // Its live examples that cannot run (a block of the language `view` whose text is no view document), each with
    // what is wrong and where in its text. A build with any writes nothing.
    std::vector<std::string> examples;
    // With a look: the families its text is set in — their files are the site's (`assets/<key>.ttf`) — and the
    // characters it shows: a site's faces are cut to what its pages show (mlibs.text.subset).
    std::vector<std::string> faces;
    std::set<char32_t>       characters;
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

// A route's page as Markdown: its source's text, the reference generated from its catalog, or — an application's
// page — the document that page holds for a reader without its program, as Markdown.
[[nodiscard]] std::expected<std::string, std::string> source_text(const std::filesystem::path& site, const Route& route);

// One route evaluated and written, the others its navigation.
[[nodiscard]] std::expected<Page, std::string> evaluate(const std::filesystem::path& site, std::span<const Route> routes, const Route& route, const SiteOptions& options = {});

// The page a static host answers a lost address with (`404.html`): the site's own `pages/404.md`, or one that says
// so and leads home. It is served at any address, so its links are from the site's own address — the path of `url`,
// said by the document (`<base>`) — and it is no route: no table lists it.
[[nodiscard]] std::expected<Page, std::string> lost_page(const std::filesystem::path& site, std::span<const Route> routes, const SiteOptions& options = {});

// The site written to `out`: every route's two files (an application's page with the files it was built with), what
// pages/ holds beside its pages (pictures) at the same place, the site's tables, `404.html` and `.nojekyll` (GitHub Pages serves the directory as it is) and, last, the delivery
// manifest of all of it (site.delivery.xdef.json).
[[nodiscard]] std::expected<Built, std::string> build(const std::filesystem::path& site, const std::filesystem::path& out, const SiteOptions& options = {});

// The routes table as its snapshot (`routes@1`), the list for agents (llms.txt: each route's title and its Markdown's
// address), and the sitemap (each route's address under `url`).
[[nodiscard]] std::string routes_json(std::span<const Route> routes, const SiteOptions& options);
[[nodiscard]] std::string llms_txt(std::span<const Route> routes, const SiteOptions& options);
// What the routes table says of these routes, without writing it: the site's title (its first route's where none is
// stated), address and languages, each route's address, source, title, language and headings.
[[nodiscard]] Table table_of(std::span<const Route> routes, const SiteOptions& options);
// Whether a directory is a site as built — it holds the routes table a build writes and no pages/ of a source: the
// reading operations then answer from what was written (its tables, each route's index.md and index.html), nothing
// evaluated — what is published is what is read.
[[nodiscard]] bool is_built(const std::filesystem::path& site);
[[nodiscard]] std::string sitemap_xml(std::span<const Route> routes, const SiteOptions& options);

// The index every route's sections make and the query over it: the shell's.
using shell::search;
using shell::search_json;

// Every route's sections, each route evaluated: what `docs.search` asks without building the site.
[[nodiscard]] std::expected<std::vector<Section>, std::string> sections_of(const std::filesystem::path& site, const SiteOptions& options = {});

// The address that leads from one route to another, as a route's files say it: "../zh/", "guide/start/", "./".
[[nodiscard]] std::string relative(std::string_view from, std::string_view to);

}  // namespace oweb
