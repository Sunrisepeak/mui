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

// A piece of a node's own text as HTML holds it: a run of text, a line's break, or the element a mark makes of a
// range — and inside it more of the same. An `a` or an `img` a mark places is the child node it names: that child
// stands here, in the text, and not again among the node's children.
struct Inline {
    std::string tag;   // empty: a run of text; "br"; "em", "strong", "code" …; "a", "img"; "span" for a mark HTML has no element for
    std::string text;  // a run's text
    std::string node;  // the child node an element here is (a link, a picture)
    std::vector<std::pair<std::string, std::string>> attributes;  // an element's
    std::vector<Inline> children;
    // The bytes of the node's text a run or an element covers (a line's break: the line feed's place, one byte): how
    // a text is cut where its lines break. Zero for what is no part of the node's text (a block of code's wrapper).
    std::size_t begin = 0, end = 0;
    bool markup = false;  // a run that is markup the application wrote: written as it is (S-3)
    bool operator==(const Inline&) const = default;
};

// Where an element stands: in the document a reader has before the program runs — nothing in it is operated, so it
// takes no focus and a control is HTML's own, disabled — or in the Web host's layer over the canvas while the
// program runs, where what a person operates (a button, a checkbox) and what the canvas alone shows (a picture)
// keep their ARIA form and the program answers for them.
enum class HtmlFor : std::uint8_t { document, live };

// A node as the one HTML element it is: the mapping the static projection and the Web host's layer share.
struct HtmlElement {
    std::string tag;  // "h2", "a", "p", "li", "table" …; "div" where HTML has no element for the node
    // The tag's own first (id, href, src, alt, title, start, align, scope, type; `disabled` and `checked` without a
    // value), then its ARIA: what the tag does not say already.
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<Inline>      content;  // its own text, before its children
    std::vector<std::string> placed;   // the children its text places (Inline::node)
    bool lines_kept = false;           // its text's lines are as written (a block of code)
    bool empty = false;                // an element that holds nothing (img, input, a rule)
};

// Finds a node by its id (a mark's link, a table's rows): nothing where there is none.
using NodeLookup = std::function<const spec::SemanticNode*(std::string_view)>;

// `inside` is the tag of the element the node stands in (a group of rows is a table's head or body only there).
[[nodiscard]] HtmlElement html_element(const spec::SemanticNode& node, std::string_view inside, const NodeLookup& find, HtmlFor where = HtmlFor::document);

struct HtmlOptions {
    std::string lang;  // the document's language as HTML says it ("en", "zh-CN"); empty: none said
};

// A snapshot (its nodes in pre-order, each naming its children) as static HTML: each node an element — a heading, a
// link, a button, a list and its items, the landmarks as HTML has them, otherwise a `div` with its role — with its ARIA
// attributes, its text, and `data-omui-target` naming it, so the program takes each over where it stands. Text and
// attributes escaped; nothing focusable before the program runs.
[[nodiscard]] std::string static_html(const Snapshot& snapshot, const HtmlOptions& options = {});

}  // namespace omui::semantic
