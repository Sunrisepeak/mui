// oweb-reader: the page a browser has, drawn (oweb.reader) — the program every page of a site loads.
import std;
import omui;
import oweb.reader;

namespace {

// The runtime the page is drawn by: who follows an address a reader pressed in the page's document.
omui::Runtime* runtime = nullptr;

}  // namespace

int main(int argc, char** argv) {
    using namespace omui;
    AppSpec app;
    app.id = "org.openmui.oweb.reader";
    app.title = "oweb";
    app.size = {960, 1200};
    app.dark_theme = Theme::dark_theme();  // as the reader's system prefers, as the page was written in both
    app.view = [] {
        return oweb::reader::view(shown_document(), [](std::string_view id) { return shown_document(id); },
                                  {.scrolls = true,
                                   .find = oweb::reader::finder({.link = [](std::string_view relation) { return shown_link(relation); },
                                                                 .file = [](std::string_view address, std::function<void(std::expected<std::string, std::string>)> done) {
                                                                     shown_file(address, std::move(done));
                                                                 }}),
                                   .follow = [](std::string href) {
                                       if (runtime) (void)runtime->open_url(href);
                                   }});
    };
    app.setup = [](Runtime& made) { runtime = &made; };
    app.teardown = [](Runtime&) { runtime = nullptr; };
    return run(std::move(app), argc, argv);
}
