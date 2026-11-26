module oweb.reader;

import std;
import omui.core;
import omui.view;

namespace oweb::reader {

shell::Finder finder(Site site) {
    struct Index {
        Site                                                                               site;
        std::string                                                                        top;       // where the site is: the index's directory
        std::optional<std::vector<shell::Section>>                                         sections;  // once it came
        bool                                                                               asked = false, lost = false;
        std::vector<std::pair<std::string, std::function<void(std::vector<shell::Found>)>>> waiting;
    };
    auto index = std::make_shared<Index>();
    index->site = std::move(site);
    return [index](std::string query, std::function<void(std::vector<shell::Found>)> answer) {
        if (index->sections) return answer(shell::found_in(*index->sections, query, index->top));
        if (index->lost) return answer({});
        index->waiting.emplace_back(std::move(query), std::move(answer));
        if (index->asked) return;
        index->asked = true;
        const auto arrived = [index](std::expected<std::string, std::string> text) {
            auto sections = text ? shell::sections_from(*text) : std::unexpected(text.error());
            if (sections) index->sections = std::move(*sections);
            else index->lost = true;
            for (auto& [words, heard] : std::exchange(index->waiting, {}))
                heard(index->sections ? shell::found_in(*index->sections, words, index->top) : std::vector<shell::Found>{});
        };
        const std::string address = index->site.link ? index->site.link("search") : std::string();
        if (address.empty() || !index->site.file) return arrived(std::unexpected("the page names no search index"));
        index->top = address.substr(0, address.rfind('/') + 1);
        index->site.file(address, arrived);
    };
}

omui::Element view(std::string_view projection, const std::function<std::string(std::string_view id)>& markup, shell::Layout layout) {
    auto parts = shell::read(projection, markup);
    if (parts) return shell::page(std::move(*parts), std::move(layout));
    // No page of a site here (the program run on its own): say so, in the shell's own place.
    layout.find = {};
    return shell::page({.document = std::format("# No page to read\n\n{}\n\nThis program draws a page of a site built by `oweb site.build --engine`.", parts.error())}, std::move(layout));
}

}  // namespace oweb::reader
