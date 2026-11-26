// oweb-reader: the page a browser has, drawn (0.1.0 plan ST2; oweb's design §3.3, D-284).
//
// The page's own HTML is its data: every written node's id with its element's label, address and words, and the
// document's markup, read back by the page (omui::shown_document). From them the shell's parts, and from the parts
// the view the builder made when it wrote the page — the same nodes by id, so the layer over the canvas takes the
// written elements over one for one, and nothing moves when the canvas takes the page.
import std;
import omui;
import oweb.shell;

int main(int argc, char** argv) {
    using namespace omui;
    AppSpec app;
    app.id = "org.openmui.oweb.reader";
    app.title = "oweb";
    app.size = {960, 1200};
    app.dark_theme = Theme::dark_theme();  // as the reader's system prefers, as the page was written in both
    app.view = [] {
        auto parts = oweb::shell::read(shown_document(), [](std::string_view id) { return shown_document(id); });
        if (parts) return oweb::shell::page(std::move(*parts), {.scrolls = true});
        // No page of a site here (the program run on its own): say so, in the shell's own place.
        return oweb::shell::page({.document = std::format("# No page to read\n\n{}\n\nThis program draws a page of a site built by `oweb site.build --engine`.", parts.error())},
                                 {.scrolls = true});
    };
    return run(std::move(app), argc, argv);
}
