// omui.ui.markdown:parser — who reads Markdown source for the component (its format `markdown`; a document given as
// HTML needs no parser): mlibs.markdown's md4c, a provider at the slot `omui.ui.markdown.parser`.
//
//     omui::install_markdown_parser();   // at the program's root, once (registering the component by name does it)
//
// A program that shows Markdown source has one: register_markdown_component() — a script's, a view document's —
// installs it, and off the Web the component finds md4c without one. On the Web a program that shows its documents as
// HTML alone (a site's reader: the page's own HTML, D-284) installs none and links no Markdown parser.
export module omui.ui.markdown:parser;

import std;
import mlibs.markdown;
import mlibs.compose;

export namespace omui {

struct MarkdownParser {
    mlibs::markdown::Parser parser;
};

inline constexpr mlibs::compose::Slot<MarkdownParser> markdown_parser_slot{{
    .id = "omui.ui.markdown.parser",
    .cardinality = mlibs::compose::Cardinality::one,
    .summary = "who reads a Markdown component's source (format markdown): mlibs.markdown's md4c",
    .absent = "install it at the program's root: omui::install_markdown_parser() (package omui.ui.markdown) — registering the "
              "component by name installs it; without one, on the Web, a Markdown source is shown as its text"}};

// Installs md4c at the slot. Again: nothing.
std::expected<void, mlibs::compose::Error> install_markdown_parser();

// What the component reads Markdown source with: the parser installed; else, off the Web, md4c; else none (`into` null).
[[nodiscard]] mlibs::markdown::Parser markdown_parser() noexcept;

}  // namespace omui

// The module's own: md4c off the Web, nothing on it (native/fallback.cpp, web/fallback.cpp) — what the component reads
// source with when no parser is installed.
namespace omui {
[[nodiscard]] mlibs::markdown::Parser fallback_parser() noexcept;
}
