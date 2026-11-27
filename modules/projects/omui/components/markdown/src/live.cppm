// omui.ui.markdown:live — a block of code that runs: a live example in a document (oweb's design §4.3, ST3).
//
// A document shows an example by its source; where someone can run that source, the document shows it running too —
// the example above, its source under it, both of the same block. Who runs a language is a provider at the slot
// `omui.ui.markdown.live` (mlibs.compose; many, by language): view documents (`view`) by this package's feature
// `views` (omui.ui.markdown.views), which carries omui.viewdoc — a program without it shows the source alone, as
// before. Only a document of the application's own (`origin: app`) runs its blocks: what a person typed or a
// remote text says is shown, not run.
//
// In the tree the block is a `figure` holding a group marked `output` (S-16: what a block gives when it runs — the
// example: its nodes are the example's own, read and operated like any) and the `code` (the source, as every block
// of code). The document the tree states is unchanged by it — an output is no part of the document's source
// (`document_of`; a writing as HTML, read back) — so its Markdown and its HTML still say the block of code.
export module omui.ui.markdown:live;

import std;
import omui.core;
import omui.view;
import mlibs.compose;

export namespace omui {

struct MarkdownLive {
    std::string_view language;  // the blocks it runs, by their language: "view"
    // The view the block's text makes — or what is wrong with the text, shown in the example's place.
    std::expected<Element, std::string> (*make)(Services& services, std::string_view text) = nullptr;
};

inline constexpr mlibs::compose::Slot<MarkdownLive> markdown_live_slot{{
    .id = "omui.ui.markdown.live",
    .cardinality = mlibs::compose::Cardinality::many,
    .summary = "who runs a document's blocks of code, by language: a live example above its source",
    .absent = "install a provider at the program's root: omui::install_markdown_views() (package omui.ui.markdown, feature "
              "`views`) runs view documents (```view); without one a block of code is shown as its source alone"}};

// The provider that runs blocks of this language, or null: none installed.
[[nodiscard]] const MarkdownLive* markdown_live(std::string_view language) noexcept;

}  // namespace omui
