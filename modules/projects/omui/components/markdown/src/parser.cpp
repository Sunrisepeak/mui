module omui.ui.markdown;

import std;
import mlibs.markdown;
import mlibs.compose;
import :parser;

namespace omui {

std::expected<void, mlibs::compose::Error> install_markdown_parser() {
    static const MarkdownParser md4c{mlibs::markdown::md4c()};
    mlibs::compose::Composition& composition = mlibs::compose::process();
    if (composition.get(markdown_parser_slot)) return {};
    return composition.install(markdown_parser_slot, md4c, "omui.ui.markdown");
}

mlibs::markdown::Parser markdown_parser() noexcept {
    if (const MarkdownParser* installed = mlibs::compose::process().get(markdown_parser_slot)) return installed->parser;
    return fallback_parser();
}

}  // namespace omui
