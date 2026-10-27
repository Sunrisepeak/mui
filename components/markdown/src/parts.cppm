// openmui.ui.markdown:parts — the nodes a document is made of (an
// implementation partition: nothing here is exported).
//
// Every block of the document is a node of its own, so the semantic tree has
// the document's structure (headings with their levels, lists and items,
// quotes, code, tables, rows and cells) and every link and task is a node an
// agent can act on. A part is built once from a copy of its block (the
// document changes as text streams in; a part's block never does: a changed
// block is a new part, its key being the block's content hash), so an
// unchanged block keeps its node, its shaped text and its layout.
module openmui.ui.markdown:parts;

import std;
import openmui.core;
import openmui.view;
import openmui.markdown;
import :markdown_contract;

namespace omui::mdview {

namespace md = omui::md;

// What the document shares with its parts.
struct Hub {
    Services*                     services = nullptr;
    NodeId                        document;
    MarkdownOrigin                origin = MarkdownOrigin::user_content;
    std::string                   base_url;
    std::map<std::string, NodeId, std::less<>> headings;  // anchor → heading node, while mounted
    std::function<void(const md::Link&)> activate_link;
    std::function<void(std::int32_t)>    toggle_task;
};

// A block and what it needs from around it: the links its marks refer to
// (renumbered from 0, so the part does not depend on the rest of the
// document), its list's tightness, its item number, its task's index.
struct Data {
    md::Block            block;
    std::vector<md::Link> links;
    bool                 tight = false;     // in a tight list
    std::uint32_t        number = 0;        // ordered item: its number
    char                 bullet = 0;        // bullet item: its list's bullet
    std::int32_t         task = 0;          // the index of the first task in the block (an item's own, if it is one)
    // A table's columns (min-content, max-content widths, padding included):
    // handed from the table to its sections and rows, which size their cells.
    std::shared_ptr<const std::vector<std::pair<float, float>>> columns;
    std::shared_ptr<Hub> hub;
};

// The block as a part sees it: a deep copy whose link references index `links`.
[[nodiscard]] std::shared_ptr<Data> localize(const md::Block& block, std::span<const md::Link> links, std::shared_ptr<Hub> hub);
// The element of a block (its id is positional: S-8, never its text).
[[nodiscard]] Element element(std::shared_ptr<Data> data, std::string id);
// The task items in a block (an item counts itself).
[[nodiscard]] std::int32_t count_tasks(const md::Block& b);

// The text style of a block, from the theme.
[[nodiscard]] typeset::TextStyle text_style(const Theme& theme, const md::Block& b);

// ── a block of text: paragraph, heading, table cell, code, raw HTML ──

class Text {
public:
    static constexpr std::string_view name = "md_text";
    static constexpr Role             role = Role::paragraph;

    std::shared_ptr<const Data> data;

    void mount(Services& s, NodeId n);
    ~Text();
    void style(const Theme& theme, layout::Style& s) const;
    void restyle() { paragraph_.reset(); }
    Size measure(MeasureContext& m, std::optional<float> w, std::optional<float> h, layout::Available aw, layout::Available ah);
    float baseline(MeasureContext& m, Size content);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& n) const;
    std::vector<Element> compose();
    void laid_out();

    // The rectangles of the text between two bytes (local coordinates).
    [[nodiscard]] std::vector<Rect> rects(std::uint32_t begin, std::uint32_t end) const;

private:
    typeset::Paragraph& shaped(Services& s) const;
    void lay(float width) const;

    Services* services_ = nullptr;
    NodeId    node_;
    mutable std::optional<typeset::Paragraph> paragraph_;
    mutable float laid_ = -1;
    mutable Point origin_{};                  // where the text starts (the content box)
    std::vector<std::pair<std::uint32_t, std::uint32_t>> link_ranges_;  // of the children, in order
    std::vector<Rect> placed_;                // the children's rectangles as last set
};

// ── an inline link or image of a block of text (positioned over its text) ──

class Link {
public:
    static constexpr std::string_view name = "md_link";
    static constexpr Role             role = Role::link;
    static constexpr bool             focusable = true;
    static constexpr std::string_view cursor = "pointer";

    std::shared_ptr<const Data> data;  // the block of text it is in
    std::size_t                 index = 0;  // its link
    bool                        image = false;
    std::vector<Rect>           rects;      // its text's lines, in its own coordinates

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& n) const;

private:
    Services* services_ = nullptr;
    NodeId    node_;
};

// ── a container: quote, list, item, table, its head, body and rows ──

class Group {
public:
    static constexpr std::string_view name = "md_group";

    std::shared_ptr<const Data> data;

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& n) const;
    std::vector<Element> compose();

private:
    Services* services_ = nullptr;
    NodeId    node_;
    mutable std::optional<typeset::Paragraph> marker_;  // an ordered item's number
};

// ── a task item's checkbox ──

class Task {
public:
    static constexpr std::string_view name = "md_task";
    static constexpr Role             role = Role::checkbox;
    static constexpr bool             focusable = true;
    static constexpr std::string_view cursor = "pointer";

    std::shared_ptr<const Data> data;  // the item

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    Size measure(MeasureContext&, std::optional<float>, std::optional<float>, layout::Available, layout::Available) { return {16, 16}; }
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    ActResult act(ActContext& ctx, std::string_view action);  // toggle
    void semantics(SemanticsContext& ctx, spec::SemanticNode& n) const;

private:
    Services* services_ = nullptr;
    NodeId    node_;
};

// ── a fenced or indented code block, with its copy button ──

class Code {
public:
    static constexpr std::string_view name = "md_code";
    static constexpr Role             role = Role::code;
    static constexpr bool             clips = true;

    std::shared_ptr<const Data> data;

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    void style(const Theme& theme, layout::Style& s) const;
    void restyle() { paragraph_.reset(); }
    Size measure(MeasureContext& m, std::optional<float> w, std::optional<float> h, layout::Available aw, layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& n) const;
    std::vector<Element> compose();

private:
    typeset::Paragraph& shaped(Services& s) const;

    Services* services_ = nullptr;
    NodeId    node_;
    mutable std::optional<typeset::Paragraph> paragraph_;
    mutable float laid_ = -1;
};

class Copy {
public:
    static constexpr std::string_view name = "md_copy";
    static constexpr Role             role = Role::button;
    static constexpr std::string_view cursor = "pointer";

    std::shared_ptr<const Data> data;  // the code block

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    ~Copy();
    bool set_prop(std::string_view k, const Value& v);  // copied: the label says so for a moment
    Size measure(MeasureContext& m, std::optional<float>, std::optional<float>, layout::Available, layout::Available);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& n) const;

private:
    typeset::Paragraph& label(Services& s) const;
    Services* services_ = nullptr;
    NodeId    node_;
    mutable std::optional<typeset::Paragraph> label_;
    bool          copied_ = false;
    std::uint64_t timer_ = 0;
};

// ── a thematic break ──

class Rule {
public:
    static constexpr std::string_view name = "md_rule";
    static constexpr Role             role = Role::separator;

    std::shared_ptr<const Data> data;

    Size measure(MeasureContext&, std::optional<float> w, std::optional<float>, layout::Available aw, layout::Available) {
        return {w.value_or(aw.is_definite() ? aw.value : 0), 1};
    }
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& n) const;
};

// The focus ring every focusable part draws (the theme's focus colour).
void focus_ring(PaintContext& ctx, Rect r, float radius);

}  // namespace omui::mdview
