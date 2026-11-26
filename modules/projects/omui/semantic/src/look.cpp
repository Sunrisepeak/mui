module omui.semantic;

import std;
import omui.core;
import omui.view;
import :tree;
import :look;

namespace omui::semantic {

namespace {

using mlibs::layout::Length;
using mlibs::layout::Style;

// A number as CSS writes it: whole where it is, else to a hundredth.
std::string number(float value) {
    const float rounded = std::round(value * 100.0f) / 100.0f;
    if (rounded == std::floor(rounded)) return std::format("{}", static_cast<long long>(rounded));
    std::string text = std::format("{:.2f}", rounded);
    while (text.ends_with('0')) text.pop_back();
    return text;
}

std::string px(float value) { return value == 0 ? std::string("0") : number(value) + "px"; }

void put(std::string& css, std::string_view property, std::string_view value) {
    css += property;
    css += ':';
    css += value;
    css += ';';
}

void length(std::string& css, std::string_view property, Length value) {
    if (value.unit == Length::Unit::points) put(css, property, px(value.value));
    else if (value.unit == Length::Unit::percent) put(css, property, number(value.value) + "%");
}

void insets(std::string& css, std::string_view property, const mlibs::layout::Insets& value) {
    if (value.top == 0 && value.right == 0 && value.bottom == 0 && value.left == 0) return;
    put(css, property, std::format("{} {} {} {}", px(value.top), px(value.right), px(value.bottom), px(value.left)));
}

std::string_view align_name(mlibs::layout::Align align) {
    using mlibs::layout::Align;
    switch (align) {
        case Align::start: return "flex-start";
        case Align::end: return "flex-end";
        case Align::center: return "center";
        case Align::stretch: return "stretch";
        case Align::baseline: return "baseline";
        case Align::auto_: return "auto";
    }
    return "auto";
}

std::string track_css(const mlibs::layout::Track& track) {
    using Kind = mlibs::layout::Track::Kind;
    const auto one = [](Kind kind, float value) -> std::string {
        switch (kind) {
            case Kind::points: return px(value);
            case Kind::percent: return number(value) + "%";
            case Kind::fr: return number(value) + "fr";
            case Kind::min_content: return "min-content";
            case Kind::max_content: return "max-content";
            default: return "auto";
        }
    };
    if (track.kind == Kind::minmax) return std::format("minmax({},{})", one(track.min_kind, track.min_value), one(track.max_kind, track.max_value));
    return one(track.kind, track.value);
}

// A node's layout style as CSS says the same thing. `in_stack`: its parent lays its children over one another.
std::string layout_css(const Style& style, bool in_stack) {
    using namespace mlibs::layout;
    std::string css;
    switch (style.display) {
        case Display::flex:
            put(css, "display", "flex");
            if (style.direction != Direction::row)
                put(css, "flex-direction", style.direction == Direction::column ? "column" : style.direction == Direction::row_reverse ? "row-reverse" : "column-reverse");
            if (style.wrap != Wrap::no_wrap) put(css, "flex-wrap", style.wrap == Wrap::wrap ? "wrap" : "wrap-reverse");
            break;
        case Display::grid: {
            put(css, "display", "grid");
            const auto tracks = [&](std::string_view property, const std::vector<Track>& all) {
                if (all.empty()) return;
                std::string value;
                for (const Track& track : all) value += (value.empty() ? "" : " ") + track_css(track);
                put(css, property, value);
            };
            tracks("grid-template-columns", style.columns);
            tracks("grid-template-rows", style.rows);
            if (style.grid_flow == GridFlow::column) put(css, "grid-auto-flow", "column");
            break;
        }
        case Display::stack:
            // Its children share one cell (below), and the cell is the stack's box: what is taller than it does not
            // make it taller (a view that scrolls inside it scrolls; it does not grow to all it holds).
            put(css, "display", "grid");
            put(css, "grid-template", "minmax(0,1fr)/minmax(0,1fr)");
            break;
        case Display::none: put(css, "display", "none"); return css;
    }
    if (style.justify != Justify::start)
        put(css, "justify-content", style.justify == Justify::end ? "flex-end" : style.justify == Justify::center ? "center" : style.justify == Justify::space_between ? "space-between"
                                    : style.justify == Justify::space_around ? "space-around" : "space-evenly");
    if (style.align_items != Align::stretch && style.align_items != Align::auto_) put(css, "align-items", align_name(style.align_items));
    if (style.row_gap != 0 || style.column_gap != 0) put(css, "gap", style.row_gap == style.column_gap ? px(style.row_gap) : std::format("{} {}", px(style.row_gap), px(style.column_gap)));
    length(css, "width", style.width);
    length(css, "height", style.height);
    length(css, "min-width", style.min_width);
    length(css, "min-height", style.min_height);
    length(css, "max-width", style.max_width);
    length(css, "max-height", style.max_height);
    if (style.aspect_ratio > 0) put(css, "aspect-ratio", number(style.aspect_ratio));
    insets(css, "margin", style.margin);
    insets(css, "padding", style.padding);
    if (style.border.top != 0 || style.border.right != 0 || style.border.bottom != 0 || style.border.left != 0) {
        put(css, "border-style", "solid");
        put(css, "border-color", "transparent");
        insets(css, "border-width", style.border);
    }
    if (style.position == Position::absolute) {
        put(css, "position", "absolute");
        length(css, "left", style.left);
        length(css, "top", style.top);
        length(css, "right", style.right);
        length(css, "bottom", style.bottom);
    } else {
        put(css, "position", "relative");  // what its absolute children are placed in
    }
    if (in_stack && style.position != Position::absolute) put(css, "grid-area", "1/1");
    if (style.grow != 0) put(css, "flex-grow", number(style.grow));
    if (style.shrink != 1) put(css, "flex-shrink", number(style.shrink));
    length(css, "flex-basis", style.basis);
    if (style.align_self != Align::auto_) put(css, "align-self", align_name(style.align_self));
    if (style.justify_self != Align::auto_) put(css, "justify-self", align_name(style.justify_self) == "flex-start" ? "start" : align_name(style.justify_self) == "flex-end" ? "end" : align_name(style.justify_self));
    if (style.overflow_x != Overflow::visible || style.overflow_y != Overflow::visible) {
        const auto name = [](Overflow overflow) { return overflow == Overflow::visible ? "visible" : overflow == Overflow::clip ? "hidden" : "auto"; };
        put(css, "overflow", style.overflow_x == style.overflow_y ? std::string(name(style.overflow_x)) : std::format("{} {}", name(style.overflow_x), name(style.overflow_y)));
    }
    return css;
}

std::string colour(const Color& color) {
    const auto byte = [](float part) { return static_cast<int>(std::lround(std::clamp(part, 0.0f, 1.0f) * 255.0f)); };
    if (color.a >= 0.999f) return std::format("#{:02x}{:02x}{:02x}", byte(color.r), byte(color.g), byte(color.b));
    return std::format("rgba({},{},{},{})", byte(color.r), byte(color.g), byte(color.b), number(color.a));
}

// A run of glyphs a node drew itself: its face, size and colour, the bytes of the node's text its glyphs set
// (the first byte of the first cluster to that of the last) and how many glyphs.
struct Run {
    mlibs::text::FontRef font;
    float                size = 0;
    Color                color;
    std::uint32_t        begin = 0, last = 0;
    std::size_t          glyphs = 0;
    [[nodiscard]] bool same_colour(const Run& other) const { return color.r == other.color.r && color.g == other.color.g && color.b == other.color.b && color.a == other.color.a; }
    [[nodiscard]] bool same_as(const Run& other) const { return font.id == other.font.id && size == other.size && same_colour(other); }
};

}  // namespace

std::string font_key(std::string_view family) {
    std::string key = "omui-";
    for (const char letter : family)
        if ((letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'z')) key += letter;
        else if (letter >= 'A' && letter <= 'Z') key += static_cast<char>(letter - 'A' + 'a');
    return key;
}

std::string_view Look::style(std::string_view id) const {
    const auto found = styles.find(std::string(id));
    return found == styles.end() ? std::string_view{} : std::string_view(found->second);
}

std::string_view Look::within(std::string_view id, std::size_t at) const {
    const auto found = spans.find(std::string(id));
    if (found == spans.end()) return {};
    for (const Span& span : found->second)
        if (span.begin <= at && at < span.end) return span.style;
    return {};
}

std::span<const Look::Wrapper> Look::around(std::string_view id) const {
    const auto found = wrappers.find(std::string(id));
    return found == wrappers.end() ? std::span<const Wrapper>{} : std::span<const Wrapper>(found->second);
}

Look html_look(const Runtime& runtime, const Snapshot& snapshot) {
    Look look;
    if (snapshot.nodes.empty()) return look;
    // The text each node drew itself: the display list brackets what a node drew (by the node's marker).
    std::unordered_map<std::uint32_t, std::vector<Run>> drawn;  // by the node's handle index
    {
        std::vector<std::uint32_t> open;
        for (const auto& command : runtime.display_list().commands()) {
            if (const auto* begin = std::get_if<mrender::BeginNode>(&command)) open.push_back(begin->node);
            else if (std::holds_alternative<mrender::EndNode>(command)) {
                if (!open.empty()) open.pop_back();
            } else if (const auto* glyphs = std::get_if<mrender::DrawGlyphs>(&command); glyphs && !open.empty() && !glyphs->glyphs.empty()) {
                const NodeId node = runtime.node_at_index(open.back());
                if (!node.valid()) continue;
                Run run{mlibs::text::FontRef{glyphs->font.id}, glyphs->size, glyphs->paint.color, glyphs->glyphs.front().cluster, glyphs->glyphs.front().cluster, glyphs->glyphs.size()};
                for (const mrender::Glyph& glyph : glyphs->glyphs) {
                    run.begin = std::min(run.begin, glyph.cluster);
                    run.last = std::max(run.last, glyph.cluster);
                }
                drawn[node.index].push_back(run);
            }
        }
    }
    // The view's node of each semantic node; the root is the view's.
    std::unordered_map<std::uint32_t, std::string_view> stated;  // the view's index → the semantic id
    std::vector<std::pair<const spec::SemanticNode*, NodeId>> nodes;
    for (const spec::SemanticNode& n : snapshot.nodes) {
        const NodeId node = &n == &snapshot.nodes.front() ? runtime.root() : runtime.find(n.id);
        if (!node.valid()) continue;
        stated.emplace(node.index, n.id);
        nodes.emplace_back(&n, node);
    }
    auto& fonts = const_cast<Runtime&>(runtime).fonts();  // reads only: the collection's lookups are not const
    const auto in_stack = [&](NodeId node) {
        const NodeId parent = runtime.parent(node);
        const Style* held = parent.valid() ? runtime.layout_style(parent) : nullptr;
        return held && held->display == mlibs::layout::Display::stack;
    };
    const auto font_of = [&](const Run& run) {
        const auto& face = fonts.face(fonts.face_of(run.font));
        Look::Font font{face.family, static_cast<int>(std::lround(fonts.weight_of(run.font))), fonts.slant_of(run.font) != mlibs::text::Slant::normal};
        if (!std::ranges::contains(look.fonts, font)) look.fonts.push_back(font);
        return font;
    };
    for (const auto& [n, node] : nodes) {
        std::string css;
        if (const Style* style = runtime.layout_style(node)) css = layout_css(*style, in_stack(node));
        if (const auto text = drawn.find(node.index); text != drawn.end()) {
            // What holds text sets it in lines, as a block does: its runs and the links in them flow together.
            if (css.starts_with("display:flex;")) css.replace(0, 13, "display:block;");
            // The node's own: what most of its glyphs were drawn with. The rest are parts of it (below).
            const std::vector<Run>& runs = text->second;
            const Run* own = &runs.front();
            std::size_t most = 0;
            for (const Run& run : runs) {
                std::size_t glyphs = 0;
                for (const Run& other : runs)
                    if (other.same_as(run)) glyphs += other.glyphs;
                if (glyphs > most) {
                    most = glyphs;
                    own = &run;
                }
            }
            const Look::Font font = font_of(*own);
            put(css, "font-family", std::format("'{}',system-ui,sans-serif", font_key(font.family)));
            put(css, "font-size", px(own->size));
            put(css, "font-weight", std::format("{}", font.weight));
            if (font.italic) put(css, "font-style", "italic");
            put(css, "color", colour(own->color));
            // Its lines as the frame set them: one line's height.
            const auto lines = runtime.text_lines(node);
            if (!lines.empty() && lines.front().bounds.height > 0) put(css, "line-height", px(lines.front().bounds.height));
            // The space between its letters: what the frame's last line is wider by than the same text set plainly
            // in the same face, over its graphemes. Read where the whole text is in one face and size.
            if (!lines.empty() && std::ranges::all_of(runs, [&](const Run& run) { return run.font.id == own->font.id && run.size == own->size; })) {
                const TextLineBox& last = lines.back();
                std::string_view said = n->name;
                if (last.begin < last.end && last.end <= said.size()) {
                    said = said.substr(last.begin, last.end - last.begin);
                    while (said.ends_with('\n') || said.ends_with('\r') || said.ends_with(' ')) said.remove_suffix(1);
                    const auto graphemes = static_cast<float>(std::ranges::count_if(last.stops, [&](const TextLineBox::Stop& stop) { return stop.begin < last.begin + said.size(); }));
                    if (graphemes > 1) {
                        mlibs::text::ParagraphStyle plain;
                        plain.style.families = {font.family};
                        plain.style.size = own->size;
                        plain.style.weight = fonts.weight_of(own->font);
                        plain.style.slant = fonts.slant_of(own->font);
                        plain.wrap = false;
                        mlibs::text::Paragraph set(fonts, std::string(said), plain);
                        set.layout(std::numeric_limits<float>::infinity());
                        const float spacing = (last.bounds.width - set.size().width) / graphemes;
                        if (std::abs(spacing) >= 0.02f && std::abs(spacing) <= own->size / 8) put(css, "letter-spacing", px(spacing));
                    }
                }
            }
            std::vector<Look::Span> parts;
            for (const Run& run : runs) {
                if (run.same_as(*own)) continue;
                const Look::Font other = font_of(run);
                std::string differs;
                if (other.family != font.family) put(differs, "font-family", std::format("'{}',system-ui,sans-serif", font_key(other.family)));
                if (run.size != own->size) put(differs, "font-size", px(run.size));
                if (other.weight != font.weight) put(differs, "font-weight", std::format("{}", other.weight));
                if (other.italic != font.italic) put(differs, "font-style", other.italic ? "italic" : "normal");
                if (!run.same_colour(*own)) put(differs, "color", colour(run.color));
                if (!differs.empty()) parts.push_back({run.begin, run.last + 1, std::move(differs)});
            }
            if (!parts.empty()) look.spans.emplace(n->id, std::move(parts));
        }
        if (!css.empty()) look.styles.emplace(n->id, std::move(css));
        // The nodes of the view between it and the nearest one the semantic tree states.
        std::vector<Look::Wrapper> around;
        for (NodeId up = runtime.parent(node); up.valid() && !stated.contains(up.index); up = runtime.parent(up)) {
            const Style* style = runtime.layout_style(up);
            around.push_back({up.index, style ? layout_css(*style, in_stack(up)) : std::string()});
        }
        if (!around.empty()) {
            std::ranges::reverse(around);
            look.wrappers.emplace(n->id, std::move(around));
        }
    }
    return look;
}

}  // namespace omui::semantic
