// oweb.reader — the page a browser has, drawn (0.1.0 plan ST2; oweb's design §3.3, D-284).
//
// The page's own HTML is its data: every written node's id with its element's label, address and words, and the
// document's markup, read back by the page (omui::shown_document). From them the shell's parts, and from the parts
// the view the builder made when it wrote the page — the same nodes by id, so the layer over the canvas takes the
// written elements over one for one, and nothing moves when the canvas takes the page.
//
//     app.view = [] { return oweb::reader::view(omui::shown_document(), [](std::string_view id) { return omui::shown_document(id); }, layout); };
export module oweb.reader;

import std;
import omui.core;
import omui.view;
export import oweb.shell;

export namespace oweb::reader {

// The site a page is of, as its program reaches it: what the page's document says is at a relation to it
// (`<link rel="search" href>`) and a file of the site by its address — the host's that showed the document
// (omui::shown_link, omui::shown_file), or a test's own.
struct Site {
    std::function<std::string(std::string_view relation)>                                                          link;
    std::function<void(std::string_view address, std::function<void(std::expected<std::string, std::string>)> done)> file;
};

// The site's search, for a page's box: the index the page's document names, fetched when words are first asked
// for — once — then every asking answered from it by the function the site's command answers with
// (oweb::shell::search), so a reader and an agent are told the same sections. Askings made while it is on its way
// are answered when it comes; an index that does not come (or is none) finds nothing, and is not asked for again.
// Each section's address is from where the index is, as the page said it ("../site.search.xdef.json": "../").
[[nodiscard]] shell::Finder finder(Site site);

// The page as its program draws it: the shell's parts read from what the page wrote (`projection`, and a node's
// markup asked by its id), in the shell's own view. Where no page of a site was written (the program run on its
// own), a page that says so.
[[nodiscard]] omui::Element view(std::string_view projection, const std::function<std::string(std::string_view id)>& markup, shell::Layout layout);

}  // namespace oweb::reader
