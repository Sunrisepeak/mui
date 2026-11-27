// omui.host.web:mirror — the semantic tree as the page's document layer (design
// 2026-11-26-web-document-layer-design.md, D-286; before it §6.2's hidden mirror): what a screen reader, the
// browser's find-in-page, a selection and the page's own tools read, since the canvas says nothing.
//
// After each batch of changes the mirror answers what changed, as one batch of operations for the page
// (src/js/omui_web.js applies it): elements removed; elements made or changed — each the HTML element the static
// projection writes for the node (omui.semantic's html_element: one mapping, so the page before the program and the
// layer over it are the same elements), its attributes (where a link leads, an anchor's id, its ARIA; what a person
// operates keeps its ARIA form), its own text with the elements its marks make, the box over the canvas (relative
// to its parent's, in CSS pixels), the children in order; where the focus is; what the live region says. Only what
// differs from what the page has is sent: a frame in which one label changed sends one attribute.
//
//   {"root":"app",
//    "remove":["main/toast"],
//    "nodes":[{"id":"main/save","tag":"div","set":{"aria-label":"Save","role":"button","tabindex":"-1"},
//              "box":[8,40,60,24]},
//             {"id":"main/doc/b1","tag":"p","box":[8,80,400,48],
//              "content":["Read ",{"tag":"a","node":"main/doc/b1/link0","c":["the guide"]}," ",{"tag":"em","c":["first"]},"."]},
//             {"id":"main/doc/b1/link0","set":{"href":"guide/"},"inline":true},
//             {"id":"main","tag":"main","children":["main/title","main/save"]}],
//    "focus":"main/save",
//    "live":"Saved",
//    "skip":{"to":"main/view/save","name":"The place"}}
//
// `tag` comes with an element when it is made (and again, with all of it, should it change). An element's own text
// is `text` where it is one run, `content` where its marks make elements of it: a run is a string, an element
// `{"tag", "c"}` — `{"tag":"br"}` a line's break — and one with `node` is that child node, standing in the text
// (`inline`) and not among `children`, with no box of its own. Where the canvas draws the text line by line the
// element's text is `lines` instead: each `{"box":[x,y,w,h],"c":[pieces]}`, the line's box relative to the
// element's, and in its pieces each word (with the white space after it) where the canvas draws it —
// `{"t":"word ","x":left,"w":width}` from the line's left edge, to a quarter of a pixel — so what the browser finds
// or a person selects is on the word the canvas shows. A line that ends where the text itself breaks (a line of
// code, a break written in a paragraph) says `"br":true`: the page sets an element's lines as one text, and that is
// where the text has a line's end of its own. A link that goes on into the next line is itself on its first line
// and an element like it (`data-omui-part` naming the node) on the next.
//
// A view that scrolls says `"scroll":[x,y,max_x,max_y]`: its element scrolls on the page too, to the same place, and
// its children's boxes are places in what it scrolls (they do not change as it scrolls). So the browser's
// find-in-page can bring a match that is out of sight into view: the page scrolls the element, says so (a request
// of kind scroll), and the view goes there.
//
// `skip` is the page's skip link: where the page's main landmark (role
// main) has something to focus, its first such node and the landmark's
// name; null once it has none.
export module omui.host.web:mirror;

import std;
import omui.core;
import omui.view;
import omui.semantic;

export namespace omui {

class DomMirror {
public:
    explicit DomMirror(Runtime& runtime);

    // What changed since the last call, as one batch for the page; nothing
    // when nothing did. The first batch makes every element and names the root.
    [[nodiscard]] std::optional<Value> sync();

    // The node the focus is on (empty: none), and its name — the hidden
    // textarea's label while it is edited.
    [[nodiscard]] std::string_view focused() const noexcept { return focused_; }
    [[nodiscard]] std::string_view focused_name() const noexcept { return focused_name_; }
    // How many elements the page has.
    [[nodiscard]] std::size_t size() const noexcept { return elements_.size(); }

private:
    using Box = std::array<std::int64_t, 4>;
    struct Element {
        std::string              tag;     // the element it is
        std::vector<std::pair<std::string, std::string>> attrs;
        std::string              text;    // its own text as sent: the content's JSON
        std::vector<std::string> children;  // the node's, all of them
        std::vector<std::string> block;     // those the page has among its children: not the ones its text places
        std::vector<std::string> placed;    // the children its text places
        bool                     inlined = false;  // it stands in its parent's text: no box of its own
        std::optional<Box>       scroll;  // a view that scrolls: its offset and how far it goes (x, y, max x, max y)
        std::array<std::int64_t, 2> extent{};  // how far, as its node states it
        std::string              parent;
        std::optional<Box>       bounds;  // its own, rounded
        Box                      box{};   // its own, or its parent's where it has none
        std::optional<Box>       sent;    // relative to the parent's, as the page has it
        std::string              spoken;  // what the live region would say of it
    };

    void place(const std::string& id, std::vector<Value>& out, std::unordered_map<std::string, std::size_t>& at);
    [[nodiscard]] std::string first_focusable(const std::string& id) const;

    semantic::Tree                           tree_;
    std::unordered_map<std::string, Element> elements_;
    std::vector<std::string>                 scrolls_;  // the views that scroll
    std::string                              focused_;
    std::string                              focused_name_;
    std::string                              main_;  // the main landmark
    std::string                              skip_;  // what the skip link was last sent as
    std::uint64_t                            synced_ = 0;  // the runtime's semantic revision last read
    bool                                     first_ = true;
};


}  // namespace omui
