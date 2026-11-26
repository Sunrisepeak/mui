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
};

struct Layout {
    // The page in a view of the window's size that scrolls it: what a canvas needs (the window does not scroll a
    // canvas). Without: the page as tall as it is, for a window that scrolls it itself.
    bool scrolls = false;
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
inline constexpr std::string_view pages_part = "nav", contents_part = "toc", turn_part = "turn", document_part = "doc";
// A part as a navigation: a landmark a reader's tools list and jump to, called `label`.
[[nodiscard]] omui::Element navigation(omui::Element part, std::string_view label);
// A part's links, each by its own id.
[[nodiscard]] std::vector<omui::Element> links(std::vector<Link> from);
// The document, in the page's `main` landmark (`main` holding `doc`); `parts.document` is taken.
[[nodiscard]] omui::Element document(Parts& parts);
// The view's root over what the page holds: a view of the window's size that scrolls it, or a column as tall as it
// is (`Layout::scrolls`). Its id is the mount's (`main`), so what it holds is named the same either way.
[[nodiscard]] omui::Element view(std::vector<omui::Element> held, Layout layout = {});

// Where an application's view is mounted.
inline constexpr std::string_view mounted_at = "main";

// The parts a page's own HTML says: `projection` as the Web host reads it (omui.host.web's page::projection — every
// written node's id, its element's label, address and words), and `markup`, the page's own markup of a node
// (page::projected), asked for the document's. A part is known by its own id wherever the view placed it: the node
// called `nav`, `toc` or `turn` names the part (its label), every link under it — at any depth — is one of the
// part's, by its own id, in the page's order; the node called `doc` is the document. Or why the page is none of a
// shell's (it has no document).
[[nodiscard]] std::expected<Parts, std::string> read(std::string_view projection, const std::function<std::string(std::string_view id)>& markup);

}  // namespace oweb::shell
