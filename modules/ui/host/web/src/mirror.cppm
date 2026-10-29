// openmui.host.web:mirror — the semantic tree as the page's hidden DOM
// (design §6.2): what a screen reader, a browser's find-in-page and the
// page's own tools read, since the canvas says nothing.
//
// After each batch of changes the mirror answers what changed, as one batch
// of operations for the page (src/js/omui_web.js applies it): elements
// removed; elements made or changed — the registry's ARIA role, the name,
// the description, the value, the states as aria-* attributes, a focusable
// node's tabindex, the box over the canvas (relative to its parent's, in CSS
// pixels), the children in order; where the focus is; what the live region
// says. Only what differs from what the page has is sent: a frame in which
// one label changed sends one attribute.
//
//   {"root":"app",
//    "remove":["main/toast"],
//    "nodes":[{"id":"main/save","set":{"aria-label":"Save","role":"button","tabindex":"-1"},
//              "box":[8,40,60,24]},
//             {"id":"main","children":["main/title","main/save"]}],
//    "focus":"main/save",
//    "live":"Saved"}
export module openmui.host.web:mirror;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;

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
        std::vector<std::pair<std::string, std::string>> attrs;  // by name
        std::string              text;
        std::vector<std::string> children;
        std::string              parent;
        std::optional<Box>       bounds;  // its own, rounded
        Box                      box{};   // its own, or its parent's where it has none
        std::optional<Box>       sent;    // relative to the parent's, as the page has it
        std::string              spoken;  // what the live region would say of it
    };

    void place(const std::string& id, std::vector<Value>& out, std::unordered_map<std::string, std::size_t>& at);

    semantic::Tree                           tree_;
    std::unordered_map<std::string, Element> elements_;
    std::string                              focused_;
    std::string                              focused_name_;
    std::uint64_t                            synced_ = 0;  // the runtime's semantic revision last read
    bool                                     first_ = true;
};

// The attributes a node's element has (by name): the registry's ARIA role,
// the name, the states, the value of a range. Exposed for the tests.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> aria_attributes(const spec::SemanticNode& node);

}  // namespace omui
