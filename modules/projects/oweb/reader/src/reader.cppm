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
export import mlibs.agent;

export namespace oweb::reader {

// The site a page is of, as its program reaches it: what the page's document says is at a relation to it
// (`<link rel="search" href>`) and a file of the site by its address — the host's that showed the document
// (omui::shown_link, omui::shown_file), or a test's own.
struct Site {
    std::function<std::string(std::string_view relation)>                                                          link;
    std::function<void(std::string_view address, std::function<void(std::expected<std::string, std::string>)> done)> file;
};

// The site with its tables kept: a file of the site that is a table (`*.xdef.json`: the routes, the search's index)
// is asked for once — askings made while it is on its way are answered when it comes, every one after from what
// came; one that did not come is asked for again by the next asking. Any other file is asked for as it is (a
// browser keeps what it fetched). What a program gives its search box and its subject both: the index a reader's
// search fetched is the one an agent's is answered from.
[[nodiscard]] Site keeping_tables(Site site);

// The site's search, for a page's box: the index the page's document names, fetched when words are first asked
// for — once — then every asking answered from it by the function the site's command answers with
// (oweb::shell::search), so a reader and an agent are told the same sections. Askings made while it is on its way
// are answered when it comes; an index that does not come (or is none) finds nothing, and is not asked for again.
// Each section's address is from where the index is, as the page said it ("../site.search.xdef.json": "../").
[[nodiscard]] shell::Finder finder(Site site);

// The site a page is of, as an agent's subject, `site` (0.1.0 plan AG5; its catalog: site.ops.xdef.ts): asked from
// inside the page — through the page's tools or a connection to it — what the `oweb` command is asked of a site
// as built: `site.describe`, `routes.list`, `page.read`, `docs.search`, answered by the same functions
// (oweb::shell's `describe_answer`, `page_answer`, `search_answer`), so a site says the same at every door. Its
// files come by `site.file`: an answer ends when its file has come; nothing is fetched until something is asked (a
// reader who asks nothing pays for none), and a file that did not come is said with the way on. Given a site
// `keeping_tables`, its two tables are fetched once. `shown`: told the words of a `docs.search`, to put them where a reader
// searches — who reads the page sees what an agent asked and what was found.
[[nodiscard]] std::unique_ptr<mlibs::agent::Subject> subject(Site site, std::function<void(std::string_view query)> shown = {});

// Words put in the page's search box, as if a reader had typed them: the box asks its finder and shows what was
// found. What the program gives `subject` as `shown`. False where the page has no box (a page that names no search).
bool show_search(omui::Runtime& runtime, std::string_view query);

// The page as its program draws it: the shell's parts read from what the page wrote (`projection`, and a node's
// markup asked by its id), in the shell's own view. Where no page of a site was written (the program run on its
// own), a page that says so.
[[nodiscard]] omui::Element view(std::string_view projection, const std::function<std::string(std::string_view id)>& markup, shell::Layout layout);

// The program a site's pages load: the page the browser has, drawn — `view` over what the page wrote, its search
// asking the site, a link pressed in its document followed by the runtime. `examples` runs a page's live examples
// (shell::install_examples: the world's program, oweb-reader), or none (oweb-reader-reading: what a page that holds
// none needs, the measure of the world's first part). The registry holds no base component by name until a page's
// example asks for them. The process's exit code.
[[nodiscard]] int program(int argc, char** argv, void (*examples)());

}  // namespace oweb::reader
