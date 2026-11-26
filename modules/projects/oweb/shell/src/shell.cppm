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

// The page as a view. Its ids: `page` the column; in it `nav` (`r…`), `toc` (`t…`), `main` holding `doc`, and `turn`
// (`before`, `after`) — each part only where it has something to show; around it `view` where the layout scrolls.
[[nodiscard]] omui::Element page(Parts parts, Layout layout = {});

// The parts a page's own HTML says: `projection` as the Web host reads it (omui.host.web's page::projection — every
// written node's id, its element's label, address and words) and the document's markup (page::projected of its
// node). Or why the page is none of this shell's (it has no document).
[[nodiscard]] std::expected<Parts, std::string> read(std::string_view projection, std::string_view document);

// The document's node in a page of this shell, with and without the view around it.
[[nodiscard]] std::string_view document_id(Layout layout = {});

}  // namespace oweb::shell
