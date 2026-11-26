// omui.semantic:look — how a drawn frame looks, as a page can say it before the program is there (UI5, D-288).
// Nothing here is written by hand for a component: every declaration is read from the frame the runtime drew —
// each node's layout style (mlibs.layout's, which is CSS's flexbox and grid model, so it is written as it is), what
// it drew over its own box (fills, gradients, borders, shadows: the display list's commands in the node's space), the
// face, size and colour of the text it drew (the display list's glyph runs, each naming the bytes of the text it
// sets) and the height of its lines. A browser lays the page out by the same model with the same faces, at whatever
// width it has, without the program.
export module omui.semantic:look;

import std;
import omui.core;
import omui.view;
import :tree;

export namespace omui::semantic {

struct Look {
    // A node of the view the semantic tree does not state (a container that only lays out): written as a `div`
    // around the nodes it holds, so that the page's boxes nest as the view's do.
    struct Wrapper {
        std::uint32_t node = 0;  // which (the view's index: nodes under the same one share its element)
        std::string   style;
    };
    // A face the frame's text is set in, as CSS names it.
    struct Font {
        std::string family;
        int         weight = 400;
        bool        italic = false;
        friend bool operator==(const Font&, const Font&) = default;
    };
    // A part of a node's text drawn otherwise than the rest (a link's colour, code's face): the bytes of the
    // node's text it covers and what differs.
    struct Span {
        std::uint32_t begin = 0, end = 0;
        std::string   style;
    };
    std::unordered_map<std::string, std::string>          styles;    // a semantic node's declarations, by its id
    std::unordered_map<std::string, std::vector<Wrapper>> wrappers;  // the unstated nodes between it and its semantic parent, outermost first
    std::unordered_map<std::string, std::vector<Span>>    spans;     // by the node's id, in the order drawn
    std::vector<Font>                                     fonts;     // in the order first drawn
    // Where the frame was read in both of a theme's schemes (`by_schemes`): what differs between them is a custom
    // property (`--o0`, `--o1`, …) in the declarations above; these say each one's value, light and dark.
    std::string light, dark;

    [[nodiscard]] std::string_view style(std::string_view id) const;
    [[nodiscard]] std::span<const Wrapper> around(std::string_view id) const;
    // What the text of node `id` at byte `at` was drawn with that the node's own declarations do not say.
    [[nodiscard]] std::string_view within(std::string_view id, std::size_t at) const;
};

// The name a page gives a family's face (`omui-inter`, `omui-jetbrainsmono`: its letters and digits, lower case), so
// that what writes the look and what declares the page's faces (by their files' names) agree without either reading
// the other's names.
[[nodiscard]] std::string font_key(std::string_view family);

// The look of the frame `runtime` last drew, for the nodes of `snapshot` (taken of the same frame): each node's
// layout, its text, and what it drew over its own box — a fill, a gradient, a border, a shadow, its opacity.
[[nodiscard]] Look html_look(const Runtime& runtime, const Snapshot& snapshot);

// The rules a page with a look states once, for the element `static_html` marks `data-omui-look` and what it holds:
// boxes sized by their borders, nothing of a browser's own margins, grounds, markers and scrollbars (each element
// says its own), a face's own optical size (as the frame set it), and — a look of both schemes — each custom
// property's dark value under the person's preference. A flex item's smallest size stays CSS's automatic one:
// mlibs.layout computes the same (CSS Flexbox §4.5). One owner: the Web package's page and oweb's pages write this.
[[nodiscard]] std::string look_sheet(const Look& look);

// One look of two — the same tree drawn light and drawn dark: a declaration whose value differs takes a custom
// property, valued for each scheme (`Look::light`, `Look::dark`), so a page follows the person's preference before
// its program does.
[[nodiscard]] Look by_schemes(Look light, const Look& dark);

}  // namespace omui::semantic
