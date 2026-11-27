// oweb.shell — a page's shell (design 2026-11-26-oweb-site-framework-design.md §3.3, §3.4; 0.1.0 plan ST2).
//
// A page of a site is its document and what stands around it: the site's pages, the page's own sections, the page
// before and the page after. `Parts` is that, as data; `page` is the view it makes. The parts are learnt twice and
// the view is made by one function: as the site is built, from its routes (oweb); and in a browser when the program
// arrives, from the page's own HTML (`read`: the projection is the page's data — nothing else is sent, D-284). So
// a node of the page as it was written is the same node, by id, of the page as it is drawn, and the layer over the
// canvas takes the written elements over one for one.
export module oweb.shell;

import std;
import omui.core;
import omui.view;

export namespace oweb::shell {

struct Link {
    std::string id;    // among its part's: "r3", "t1", "before"
    std::string href;  // as the page writes it: "../start/", "#install"
    std::string text;
};

struct Parts {
    std::string       site;      // what the pages' navigation is called: the site's title
    std::vector<Link> pages;     // the site's pages; none: a site of one page
    std::string       contents;  // what the sections' navigation is called, in the page's language
    std::vector<Link> sections;  // the page's own sections
    std::string       turn;      // what the page before and after are called together
    std::vector<Link> beside;    // the page before, the page after
    std::string       document;  // the page's document: Markdown, or
    bool              html = false;  // … its HTML, as the page wrote it
    std::string       search;    // what the site's search is called, in the page's language; empty: the page has none
};

// A section a page's search found, as the page shows it: where it is from the page, its title, the headings it is
// under and the line that holds the words.
struct Found {
    std::string href, title, trail, line;
};
// Who answers a page's search box: asked with the box's words at each change, it answers at once or later (the
// site's index is fetched when it is first wanted) — and again only for the words asked last.
using Finder = std::function<void(std::string query, std::function<void(std::vector<Found>)> answer)>;

// What runs a page's live examples, the shell's own: its blocks of the language `view` are view documents over the
// base components, found by name (a page's examples are its data: which components they use is not known to the
// program) — every base component registered by name, and omui.ui.markdown's view documents installed at its slot.
void install_examples();

// Whether a page's document holds a live example (a block of the language `view`, as Markdown writes its fence or
// as the page's HTML marks its code): what `install_examples` is wanted for.
[[nodiscard]] bool holds_examples(std::string_view document, bool html);

struct Layout {
    // The page in a view of the window's size that scrolls it: what a canvas needs (the window does not scroll a
    // canvas). Without: the page as tall as it is, for a window that scrolls it itself.
    bool scrolls = false;
    // The page's search, where a program has the page: without one the box is there and finds nothing (a page as
    // it is written — only a program answers a search).
    Finder find;
    // Who follows a link a reader pressed in the page's document (`../ui/`, `#install`, `https://…`): the program
    // that has the page hands it to its runtime (`Runtime::open_url`: the page's own in this tab, another's as a
    // browser opens it). Without: a link in the document is said and goes nowhere (a page as it is written).
    std::function<void(std::string href)> follow;
    // Who runs a page's live examples, called once — when the first page that holds one is made: the shell's own by
    // default. A program that never runs one says `.examples = nullptr` (a designated initializer, so the default is
    // not named) and carries none of it: an example is then shown as its source, as on a page without a program. The
    // program a site's pages load has two programs of its own this way, the second the measure of what the examples
    // add (oweb.reader: its world in parts, a page with no example never fetching theirs).
    void (*examples)() = &install_examples;
};

// The page as a view, for where an application's view is mounted (`main`). Its ids there: `main/page` the column; in
// it `nav` (`r…`), `toc` (`t…`), `main` holding `doc`, and `turn` (`before`, `after`) — each part only where it has
// something to show. The same in either layout.
[[nodiscard]] omui::Element page(Parts parts, Layout layout = {});

// A page's view: its parts, placed. `page` is the shell's own; a site's own is any function of this shape (a
// navigation at the side, the contents beside the document, parts left out) that keeps the parts' names — see
// `read`. The builder and the program a page loads are given the same one (oweb's SiteOptions::view; the program's
// own `app.view`), so a page as written is the page as drawn whatever its view.
using View = std::function<omui::Element(Parts, Layout)>;

// What `page` is made of, for a view of one's own.
// A part's own id, wherever the view places it: the site's pages, the page's sections, the page before and after,
// the document.
inline constexpr std::string_view pages_part = "nav", contents_part = "toc", turn_part = "turn", document_part = "doc", search_part = "search";
// A part as a navigation: a landmark a reader's tools list and jump to, called `label`.
[[nodiscard]] omui::Element navigation(omui::Element part, std::string_view label);
// The site's search: a box called `label` and, under it, what `find` answers for its words — each section a link
// (`h0`…) with the headings it is under and the line that holds the words. Nothing found says so.
[[nodiscard]] omui::Element search_box(std::string label, Finder find);
// A part's links, each by its own id.
[[nodiscard]] std::vector<omui::Element> links(std::vector<Link> from);
// The document, in the page's `main` landmark (`main` holding `doc`); `parts.document` is taken.
[[nodiscard]] omui::Element document(Parts& parts, const Layout& layout = {});
// The view's root over what the page holds: a view of the window's size that scrolls it, or a column as tall as it
// is (`Layout::scrolls`). Its id is the mount's (`main`), so what it holds is named the same either way.
[[nodiscard]] omui::Element view(std::vector<omui::Element> held, Layout layout = {});

// Where an application's view is mounted.
inline constexpr std::string_view mounted_at = "main";

// The parts a page's own HTML says: `projection` as the Web host reads it (omui.host.web's page::projection — every
// written node's id, its element's label, address and words), and `markup`, the page's own markup of a node
// (page::projected), asked for the document's. A part is known by its own id wherever the view placed it: the node
// called `nav`, `toc` or `turn` names the part (its label), every link under it — at any depth — is one of the
// part's, by its own id, in the page's order; the node called `search` names the site's search; the node called
// `doc` is the document. Or why the page is none of a shell's (it has no document).
[[nodiscard]] std::expected<Parts, std::string> read(std::string_view projection, const std::function<std::string(std::string_view id)>& markup);

// A part of a page a reader can be sent to: a heading and what stands under it up to the next heading. What a
// person's search box and an agent's query both answer with (one index, made from the tree: 0.1.0 plan ST5).
struct Section {
    std::string              route;   // "/guide/start/"
    std::string              anchor;  // the heading's; empty: the page's beginning
    std::string              title;   // the heading's text; the page's title for what stands before any
    std::vector<std::string> trail;   // the headings it is under, the page's first
    std::string              text;    // what it holds, a line a block
};

// A section a query found: how well, and the line of it that says so.
struct Hit {
    Section     section;
    double      score = 0;
    std::string excerpt;
};

// The index every route's sections make (`search@1`): what a built site serves as site.search.xdef.json.
[[nodiscard]] std::string search_json(std::span<const Section> sections);

// The sections that hold every word of the query, best first: a word of Latin letters and digits matches whole or
// as the start of a word, without case; text without spaces between words (Chinese, Japanese) matches by its
// characters in a row. A word in a title or a trail counts more than one in the text.
[[nodiscard]] std::vector<Hit> search(std::span<const Section> sections, std::string_view query, std::size_t limit = 8);

// A finder over an index's sections: `search` asked for the words, each section's address from `top` (where the
// site is, as the page finds it: "../", "https://example.org/docs/").
[[nodiscard]] std::vector<Found> found_in(std::span<const Section> sections, std::string_view query, std::string_view top, std::size_t limit = 8);

// The sections an index holds (what `search_json` wrote; a site's site.search.xdef.json), or why it is none.
[[nodiscard]] std::expected<std::vector<Section>, std::string> sections_from(std::string_view index);

// A heading of a page: its level, its text, the anchor it is reached by.
struct Heading {
    int         level = 1;
    std::string text;
    std::string anchor;
};

// A route as a site's table lists it.
struct Entry {
    std::string          path;      // where it is served: "/guide/start/"
    std::string          source;    // its page as the table says it: "pages/guide/start.md"
    std::string          title;
    std::string          language;
    std::vector<Heading> headings;
};

// A site as its routes table says it (`routes@1`: the site.routes.xdef.json a build writes): what it is called,
// where it is served, its languages, its routes in the navigation's order.
struct Table {
    std::string              title;
    std::string              url;  // empty: the site states none
    std::vector<std::string> languages;
    std::vector<Entry>       routes;
};
// The table a routes table's text holds, or why it is none.
[[nodiscard]] std::expected<Table, std::string> table_from(std::string_view routes_table);

// A site described to whoever asks, at whichever of its doors (0.1.0 plan AG5): its title; that every page is also
// plain Markdown and where its two tables are; then each route's title with its Markdown's address — from `url`
// where the site states one, else from the site's own top. A build writes this text as llms.txt, and
// `site.describe` answers with it: one description, said the same at every door.
[[nodiscard]] std::string described(const Table& table);

// What the site's reading operations answer with (0.1.0 plan AG5), each a JSON object: the command and a page's own
// subject answer with these, so a site says the same to whoever asks, from wherever.
// `site.describe`: the table's facts (title, address, languages, each route with its Markdown's address), whether
// what answers is the site as built, where its two tables are, the requests to go on with (`next`: a JSON array,
// written as the door that answers takes them) and `text` — the description, `described`.
[[nodiscard]] std::string describe_answer(const Table& table, bool built, std::string_view next);
// `routes.list`: each route as `entry_json` says it.
[[nodiscard]] std::string routes_answer(const Table& table);
// `docs.search`: the sections found for the words, best first — each its route, anchor, title, the headings it is
// under, the line that says so and where its page's Markdown is — and how many sections were asked.
[[nodiscard]] std::string search_answer(std::span<const Section> sections, std::string_view query, std::size_t limit);
// `page.read` of a route as it was written: what the table says of the route, and the text (`as`: markdown or html).
[[nodiscard]] std::string page_answer(const Entry& route, std::string_view as, std::string_view text);
// A route as the operations list it: its address, source, title, language and headings.
[[nodiscard]] std::string entry_json(const Entry& route);

}  // namespace oweb::shell
