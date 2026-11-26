// omui.ui.markdown:read — a semantic tree read back as the document it states (D-258, UI2). What a node of a
// document is beyond its text is in the tree (its `properties` and `marks`, S-16 and S-17), so the document can be
// written from the tree alone — as Markdown here, as HTML by omui.semantic — and no writing loses what the tree
// holds: for a Markdown component's own subtree, parsing `markdown_of` gives the document the component was given
// (the tests hold the two documents' HTML equal, a line's end inside a paragraph read as the space the tree has).
//
//     const auto snapshot = tree.snapshot({.root = "main/doc"});
//     std::string text = omui::markdown_of(snapshot.nodes);   // what an agent's `read` answers
export module omui.ui.markdown:read;

import std;
import omui.core;
import mlibs.markdown;

export namespace omui {

// The document the nodes state: `nodes` in pre-order, the first the root to read from (a snapshot's are so). A
// heading, a paragraph, a quote, a list and its items, a table, a block of code, a rule, a picture, a comment are the
// blocks; a node that is none of these passes its children through; what a view adds to operate the document (a
// button that copies, a task's checkbox) is no part of it. A text's marks are its spans, a link's or an image's
// address the child node's the mark names.
[[nodiscard]] mlibs::markdown::Document document_of(std::span<const spec::SemanticNode> nodes);

// The same, written as Markdown (mlibs.markdown's to_markdown).
[[nodiscard]] std::string markdown_of(std::span<const spec::SemanticNode> nodes);

}  // namespace omui
