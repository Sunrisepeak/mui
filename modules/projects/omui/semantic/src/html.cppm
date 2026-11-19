// omui.semantic:html — the semantic tree as HTML (D-236, G2). What a node is in ARIA and the text its element holds:
// what the Web host's live mirror gives each node (omui.host.web), and a static projection of a snapshot — what a
// reader without JavaScript, or a search engine, reads before the program runs. One mapping for both, so the page's
// text before and after the program starts is the semantic tree's.
export module omui.semantic:html;

import std;
import omui.core;
import omui.view;
import :tree;

export namespace omui::semantic {

// A node's ARIA attributes, sorted by name: its role (none for the root, which is no `application`, and none for a
// generic container), its name where its role takes one, its states, a range's value and bounds, a heading's level.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> aria_attributes(const spec::SemanticNode& node);

// The text an element holds: its name where ARIA gives its role none (a paragraph, a generic run of text), and its
// value where it is no range's (a text box's text).
[[nodiscard]] std::string element_text(const spec::SemanticNode& node);

struct HtmlOptions {
    std::string lang;  // the document's language as HTML says it ("en", "zh-CN"); empty: none said
};

// A snapshot (its nodes in pre-order, each naming its children) as static HTML: each node an element — a heading, a
// link, a button, a list and its items, the landmarks as HTML has them, otherwise a `div` with its role — with its ARIA
// attributes, its text, and `data-omui-target` naming it, so the program takes each over where it stands. Text and
// attributes escaped; nothing focusable before the program runs.
[[nodiscard]] std::string static_html(const Snapshot& snapshot, const HtmlOptions& options = {});

}  // namespace omui::semantic
