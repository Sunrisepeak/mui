module omui.ui.markdown.views;

import std;
import mlibs.compose;
import omui.core;
import omui.view;
import omui.viewdoc;
import omui.ui.markdown;

namespace omui {

std::expected<void, mlibs::compose::Error> install_markdown_views() {
    static const MarkdownLive views{.language = "view", .make = [](Services& services, std::string_view text) -> std::expected<Element, std::string> {
                                        auto view = viewdoc::View::load(services, text);
                                        if (view) return view->element();  // the element holds the view's state
                                        std::string said;
                                        for (const viewdoc::Diagnostic& problem : view.error()) said += std::format("{}{}", said.empty() ? "" : "\n", problem.text());
                                        return std::unexpected(std::move(said));
                                    }};
    mlibs::compose::Composition& composition = mlibs::compose::process();
    for (const MarkdownLive* installed : composition.all(markdown_live_slot))
        if (installed == &views) return {};
    return composition.install(markdown_live_slot, views, "omui.ui.markdown.views");
}

}  // namespace omui
