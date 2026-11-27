// oweb.reader's program (main.cpp, reading/main.cpp): the page a browser has, drawn — on the SDK's host.
module oweb.reader;

import std;
import omui;

namespace oweb::reader {

namespace {

// The runtime the page is drawn by: who follows an address a reader pressed in the page's document.
omui::Runtime* runtime = nullptr;

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
                     .find = finder({.link = [](std::string_view relation) { return shown_link(relation); },
                                     .file = [](std::string_view address, std::function<void(std::expected<std::string, std::string>)> done) {
                                         shown_file(address, std::move(done));
                                     }}),
                     .follow = [](std::string href) {
                         if (runtime) (void)runtime->open_url(href);
                     },
                     .examples = examples});
    };
    app.setup = [](Runtime& made) { runtime = &made; };
    app.teardown = [](Runtime&) { runtime = nullptr; };
    return run(std::move(app), argc, argv);
}

}  // namespace oweb::reader
