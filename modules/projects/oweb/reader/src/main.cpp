// oweb-reader: the page a browser has, drawn (0.1.0 plan ST2; oweb's design §3.3, D-284).
//
// The page's own HTML is its data: every written node's id with its element's label, address and words, and the
// document's markup, read back by the page (omui::shown_document). From them the shell's parts, and from the parts
// the view the builder made when it wrote the page — the same nodes by id, so the layer over the canvas takes the
// written elements over one for one, and nothing moves when the canvas takes the page.
import std;
import omui;
import oweb.shell;

namespace {

// The site's search, for the page's box: the index the page's own document names (`<link rel="search">`), fetched
// by the page when words are first asked for, then every asking answered from it — by the function the site's
// command answers with (oweb::shell::search), so a reader and an agent are told the same sections.
oweb::shell::Finder finder() {
    struct Index {
        std::string                                                                top;       // where the site is: the index's directory
        std::optional<std::vector<oweb::shell::Section>>                           sections;  // once it came
        bool                                                                       asked = false, lost = false;
        std::vector<std::pair<std::string, std::function<void(std::vector<oweb::shell::Found>)>>> waiting;
    };
    auto index = std::make_shared<Index>();
    return [index](std::string query, std::function<void(std::vector<oweb::shell::Found>)> answer) {
        if (index->sections) return answer(oweb::shell::found_in(*index->sections, query, index->top));
        if (index->lost) return answer({});
        index->waiting.emplace_back(std::move(query), std::move(answer));
        if (index->asked) return;
        index->asked = true;
        const std::string address = omui::shown_link("search");
        index->top = address.substr(0, address.rfind('/') + 1);
        omui::shown_file(address, [index](std::expected<std::string, std::string> text) {
            auto sections = text ? oweb::shell::sections_from(*text) : std::unexpected(text.error());
            if (sections) index->sections = std::move(*sections);
            else index->lost = true;
            for (auto& [words, heard] : std::exchange(index->waiting, {}))
                heard(index->sections ? oweb::shell::found_in(*index->sections, words, index->top) : std::vector<oweb::shell::Found>{});
        });
    };
}

// The runtime the page is drawn by: who follows an address a reader pressed in the page's document.
omui::Runtime* runtime = nullptr;

oweb::shell::Layout layout() {
    return {.scrolls = true, .find = finder(), .follow = [](std::string href) {
                if (runtime) (void)runtime->open_url(href);
            }};
}

}  // namespace

int main(int argc, char** argv) {
    using namespace omui;
    AppSpec app;
    app.id = "org.openmui.oweb.reader";
    app.title = "oweb";
    app.size = {960, 1200};
    app.dark_theme = Theme::dark_theme();  // as the reader's system prefers, as the page was written in both
    app.view = [] {
        auto parts = oweb::shell::read(shown_document(), [](std::string_view id) { return shown_document(id); });
        if (parts) return oweb::shell::page(std::move(*parts), layout());
        // No page of a site here (the program run on its own): say so, in the shell's own place.
        return oweb::shell::page({.document = std::format("# No page to read\n\n{}\n\nThis program draws a page of a site built by `oweb site.build --engine`.", parts.error())},
                                 {.scrolls = true, .find = {}});
    };
    app.setup = [](Runtime& made) { runtime = &made; };
    app.teardown = [](Runtime&) { runtime = nullptr; };
    return run(std::move(app), argc, argv);
}
