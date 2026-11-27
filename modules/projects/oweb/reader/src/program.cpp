// oweb.reader's program (main.cpp, reading/main.cpp): the page a browser has, drawn — on the SDK's host.
module oweb.reader;

import std;
import omui;

namespace oweb::reader {

namespace {

// The runtime the page is drawn by: who follows an address a reader pressed in the page's document.
omui::Runtime* runtime = nullptr;

// The site as the page's host reaches it — what the document names, a file by the page's own fetch — its tables
// kept: one for the page's search box and for the subject.
const Site& shown_site() {
    static const Site kept = keeping_tables({.link = [](std::string_view relation) { return omui::shown_link(relation); },
                                             .file = [](std::string_view address, std::function<void(std::expected<std::string, std::string>)> done) {
                                                 omui::shown_file(address, std::move(done));
                                             }});
    return kept;
}

// The site as a subject of the page's runtime: asked beside the interface, on the same connection.
std::unique_ptr<mlibs::agent::Subject> site;

}  // namespace

int program(int argc, char** argv, void (*examples)()) {
    using namespace omui;
    // No base component by name (not the default's, which would carry every one): a page's live example registers
    // what it runs (shell::install_examples), and a page that holds none needs none.
    AppSpec app{.components = nullptr};
    app.id = "org.openmui.oweb.reader";
    app.title = "oweb";
    app.size = {960, 1200};
    app.dark_theme = Theme::dark_theme();  // as the reader's system prefers, as the page was written in both
    app.view = [examples] {
        return view(shown_document(), [](std::string_view id) { return shown_document(id); },
                    {.scrolls = true,
                     .find = finder(shown_site()),
                     .follow = [](std::string href) {
                         if (runtime) (void)runtime->open_url(href);
                     },
                     .examples = examples});
    };
    app.setup = [](Runtime& made) {
        runtime = &made;
        // What an agent searched for is put in the page's own box: the reader sees the words and what they found.
        site = subject(shown_site(), [](std::string_view query) {
            if (runtime) (void)show_search(*runtime, query);
        });
        made.add_subject(*site);
    };
    app.teardown = [](Runtime& made) {
        if (site) made.remove_subject(*site);
        site.reset();
        runtime = nullptr;
    };
    return run(std::move(app), argc, argv);
}

}  // namespace oweb::reader
