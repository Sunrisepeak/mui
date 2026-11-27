module omui.semantic;

import std;
import omui.core;
import omui.view;
import mlibs.unicode;
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

// A node's layout style as CSS says the same thing. `in_stack`: its parent lays its children over one another;
// `in_row`: its parent sets them in a row.
std::string layout_css(const Style& style, bool in_stack, bool in_row) {
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
    // An item that does not grow counts in its row's own width by its basis, as the engine sizes the row (a table's
    // columns: every row as wide as the widest cells make it). CSS counts the item's content there unless its width
    // says more — a row a few pixels narrower, each item shrunk under what its words need.
    if (in_row && style.grow == 0) {
        std::string own, by_basis;
        length(own, "width", style.width);
        length(by_basis, "width", style.basis);
        if (own.empty() && by_basis.ends_with("px;")) css += by_basis;
    }
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
// (the first byte of the first cluster to that of the last), how many glyphs, the first byte of each one's cluster,
// each one's id, and where the first stands on its baseline in the node's box (none: under a transform of its own).
struct Run {
    mlibs::text::FontRef       font;
    float                      size = 0;
    Color                      color;
    std::uint32_t              begin = 0, last = 0;
    std::size_t                glyphs = 0;
    std::vector<std::uint32_t> clusters;
    std::vector<std::uint32_t> ids;
    std::optional<Point>       at;
    [[nodiscard]] bool same_colour(const Run& other) const { return color.r == other.color.r && color.g == other.color.g && color.b == other.color.b && color.a == other.color.a; }
    [[nodiscard]] bool same_as(const Run& other) const { return font.id == other.font.id && size == other.size && same_colour(other); }
};

// A node's own drawing as CSS says it: what it drew over its own box (the view's background, border and shadow, and
// what its component painted there), read from the frame's commands in the node's own space.
struct Own {
    std::optional<Rect>      frame;       // its border box, as the frame says it
    int                      moved = 0;   // transforms pushed since: what is drawn under one is not its own box's
    float                    opacity = 1;
    std::optional<Color>     fill;        // the first solid fill of the whole box, under everything else
    std::optional<CornerRadii> radii;
    // What lies over the fill, in the order drawn: each a background layer, of the whole box or of a part of it.
    struct Picture {
        bool        whole = false;
        std::string css;
    };
    std::vector<Picture>     pictures;
    // The parts it drew are not its box's: it drew glyphs, and they are its text's (a link's underline) — the
    // elements' own on a page — or it scrolls, and they are where it is scrolled to (its bar).
    bool                     parts_apart = false;
    std::vector<std::string> shadows;     // in the order drawn
    std::vector<std::string> rings;       // borders: strokes along the inside of the box's edge
};

// White space as Unicode's word segmentation says it, line feeds and tabs too: what the engine sets beside the text
// it follows (mlibs.text's shaper), not by the faces its style names.
bool white(char32_t character) {
    using mlibs::unicode::Wb;
    const Wb kind = mlibs::unicode::word_class(character);
    return kind == Wb::WSegSpace || kind == Wb::CR || kind == Wb::LF || kind == Wb::Newline || character == U'\t' || character == U'\u00A0';
}

bool about(float a, float b) { return std::abs(a - b) <= 0.51f; }
bool same_box(const Rect& a, const Rect& b) { return about(a.x, b.x) && about(a.y, b.y) && about(a.x + a.width, b.x + b.width) && about(a.y + a.height, b.y + b.height); }

// A gradient over `box` as CSS writes it: the same line and the same stops. None: a kind CSS says otherwise.
std::optional<std::string> gradient_css(const mrender::Gradient& gradient, const Rect& box) {
    if (gradient.spread != mrender::Spread::pad || gradient.stops.empty()) return std::nullopt;
    const auto stops = [&](float from, float to) {
        std::string out;
        for (const mrender::GradientStop& stop : gradient.stops) out += std::format(",{} {}%", colour(stop.color), number((from + stop.offset * (to - from)) * 100));
        return out;
    };
    if (gradient.kind == mrender::GradientKind::linear) {
        const float dx = gradient.end.x - gradient.start.x, dy = gradient.end.y - gradient.start.y;
        const float length = std::hypot(dx, dy);
        if (!(length > 0)) return std::nullopt;
        // CSS's line goes through the box's middle, as long as the box is deep along it; the stops stand where the
        // gradient's own ends fall on that line.
        const float ux = dx / length, uy = dy / length;
        const float line = std::abs(box.width * ux) + std::abs(box.height * uy);
        if (!(line > 0)) return std::nullopt;
        const auto along = [&](Point at) { return ((at.x - (box.x + box.width / 2)) * ux + (at.y - (box.y + box.height / 2)) * uy) / line + 0.5f; };
        const float degrees = std::atan2(ux, -uy) * 180.0f / std::numbers::pi_v<float>;
        return std::format("linear-gradient({}deg{})", number(degrees), stops(along(gradient.start), along(gradient.end)));
    }
    if (gradient.kind == mrender::GradientKind::radial && gradient.radius > 0 && about(gradient.start.x, gradient.end.x) && about(gradient.start.y, gradient.end.y)) {
        std::string out = std::format("radial-gradient(circle {} at {} {}", px(gradient.radius), px(gradient.start.x - box.x), px(gradient.start.y - box.y));
        for (const mrender::GradientStop& stop : gradient.stops) out += std::format(",{} {}%", colour(stop.color), number(stop.offset * 100));
        return out + ")";
    }
    return std::nullopt;
}

// What paints `rect` of the box as one background layer: the picture, where it lies, how large. An edge that is
// the box's own stays the box's at any size (a bar down its side is as tall as the box becomes).
std::string layer(const std::string& picture, const Rect& rect, const Rect& box) {
    const bool left = about(rect.x, box.x), right = about(rect.x + rect.width, box.x + box.width);
    const bool top = about(rect.y, box.y), bottom = about(rect.y + rect.height, box.y + box.height);
    const std::string width = left && right ? "100%" : right ? std::format("calc(100% - {})", px(rect.x - box.x)) : px(rect.width);
    const std::string height = top && bottom ? "100%" : bottom ? std::format("calc(100% - {})", px(rect.y - box.y)) : px(rect.height);
    return std::format("{} {} {}/{} {} no-repeat", picture, px(rect.x - box.x), px(rect.y - box.y), width, height);
}

std::string radii_css(const CornerRadii& radii) {
    if (radii.top_left == radii.top_right && radii.top_left == radii.bottom_right && radii.top_left == radii.bottom_left) return px(radii.top_left);
    return std::format("{} {} {} {}", px(radii.top_left), px(radii.top_right), px(radii.bottom_right), px(radii.bottom_left));
}

// What a paint is as a picture: a solid colour as a gradient of it (a background layer is an image), a gradient.
std::optional<std::string> picture_of(const mrender::Paint& paint, const Rect& rect) {
    if (paint.kind == mrender::Paint::Kind::solid) return paint.color.a > 0 ? std::optional(std::format("linear-gradient({0},{0})", colour(paint.color))) : std::nullopt;
    if (paint.kind == mrender::Paint::Kind::gradient) return gradient_css(paint.gradient, rect);
    return std::nullopt;
}

void fills(Own& own, const Rect& rect, const CornerRadii* radii, const mrender::Paint& paint) {
    const bool whole = same_box(rect, *own.frame);
    if (whole && radii && !radii->zero() && !own.radii) own.radii = *radii;
    if (whole && paint.kind == mrender::Paint::Kind::solid && !own.fill && own.pictures.empty()) {
        if (paint.color.a > 0) own.fill = paint.color;
        return;
    }
    // A rounded shape that is a part of the box: one that runs from an edge of the box to the edge across (a bar
    // down its side) is a layer as a rectangle is, its small corners left; any other (a list's bullet) is a small
    // picture of its own size, where it was drawn.
    if (!whole && radii && !radii->zero()) {
        const Rect& box = *own.frame;
        const bool across = (about(rect.x, box.x) && about(rect.x + rect.width, box.x + box.width)) || (about(rect.y, box.y) && about(rect.y + rect.height, box.y + box.height));
        if (!across) {
            if (paint.kind != mrender::Paint::Kind::solid || !(paint.color.a > 0)) return;
            std::string fill = colour(paint.color);
            if (fill.starts_with('#')) fill.replace(0, 1, "%23");
            const std::string picture = std::format("url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 {0} {1}'%3E%3Crect width='{0}' height='{1}' rx='{2}' fill='{3}'/%3E%3C/svg%3E\")",
                                                    number(rect.width), number(rect.height), number(std::min({radii->top_left, rect.width / 2, rect.height / 2})), fill);
            own.pictures.push_back({false, layer(picture, rect, box)});
            return;
        }
    }
    if (const auto picture = picture_of(paint, rect)) own.pictures.push_back({whole, layer(*picture, rect, *own.frame)});
}

std::string own_css(const Own& own) {
    std::string css;
    std::string all;
    for (const Own::Picture& picture : own.pictures | std::views::reverse)  // CSS lists the top one first
        if (picture.whole || !own.parts_apart) all += (all.empty() ? "" : ",") + picture.css;
    if (own.fill && all.empty()) put(css, "background-color", colour(*own.fill));
    if (!all.empty()) put(css, "background", own.fill ? std::format("{} {}", all, colour(*own.fill)) : all);
    if (own.radii) put(css, "border-radius", radii_css(*own.radii));
    if (!own.rings.empty() || !own.shadows.empty()) {
        std::string all;
        for (const std::string& ring : own.rings | std::views::reverse) all += (all.empty() ? "" : ",") + ring;
        for (const std::string& shadow : own.shadows | std::views::reverse) all += (all.empty() ? "" : ",") + shadow;
        put(css, "box-shadow", all);
    }
    if (own.opacity < 0.999f) put(css, "opacity", number(own.opacity));
    return css;
}

// Two declarations lists of one node, drawn light and drawn dark, as one: what differs is a custom property, said
// once for each scheme (`names`: its two values, in the order first met).
std::string by_scheme(std::string_view light, std::string_view dark, std::vector<std::pair<std::string, std::string>>& names) {
    const auto parts = [](std::string_view css) {
        std::vector<std::pair<std::string_view, std::string_view>> out;
        while (!css.empty()) {
            const auto end = css.find(';');
            const std::string_view one = css.substr(0, end);
            if (const auto colon = one.find(':'); colon != std::string_view::npos) out.emplace_back(one.substr(0, colon), one.substr(colon + 1));
            if (end == std::string_view::npos) break;
            css.remove_prefix(end + 1);
        }
        return out;
    };
    const auto lit = parts(light), unlit = parts(dark);
    if (lit.size() != unlit.size()) return std::string(light);
    for (std::size_t at = 0; at < lit.size(); ++at)
        if (lit[at].first != unlit[at].first) return std::string(light);
    std::string css;
    for (std::size_t at = 0; at < lit.size(); ++at) {
        if (lit[at].second == unlit[at].second) {
            put(css, lit[at].first, lit[at].second);
            continue;
        }
        const std::pair<std::string, std::string> values{std::string(lit[at].second), std::string(unlit[at].second)};
        auto found = std::ranges::find(names, values);
        if (found == names.end()) {
            names.push_back(values);
            found = names.end() - 1;
        }
        put(css, lit[at].first, std::format("var(--o{})", found - names.begin()));
    }
    return css;
}

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

std::span<const Look::Span> Look::set_apart(std::string_view id) const {
    const auto found = apart.find(std::string(id));
    return found == apart.end() ? std::span<const Span>{} : std::span<const Span>(found->second);
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
    std::unordered_map<std::uint32_t, std::string>      painted;  // its own box's drawing, by the same
    // The solid shapes a node drew over a part of its box (in its box's space): where it drew text, the ground of a
    // part of it (inline code's tinted rectangle), which the page's element for that part draws as its own.
    struct Ground {
        Rect        rect;
        CornerRadii radii;
        Color       color;
    };
    std::unordered_map<std::uint32_t, std::vector<Ground>> grounds;
    const auto ground = [&](std::uint32_t index, const Own& own, const Rect& rect, const CornerRadii& radii, const mrender::Paint& paint) {
        if (paint.kind != mrender::Paint::Kind::solid || !(paint.color.a > 0) || same_box(rect, *own.frame)) return;
        const NodeId node = runtime.node_at_index(index);
        if (!node.valid()) return;
        grounds[node.index].push_back({Rect{rect.x - own.frame->x, rect.y - own.frame->y, rect.width, rect.height}, radii, paint.color});
    };
    {
        std::vector<std::pair<std::uint32_t, Own>> open;
        for (const auto& command : runtime.display_list().commands()) {
            if (const auto* begin = std::get_if<mrender::BeginNode>(&command)) {
                open.emplace_back(begin->node, Own{});
                continue;
            }
            if (std::holds_alternative<mrender::EndNode>(command)) {
                if (open.empty()) continue;
                if (const NodeId node = runtime.node_at_index(open.back().first); node.valid()) {
                    using mlibs::layout::Overflow;
                    const auto scrolls = [](Overflow overflow) { return overflow != Overflow::visible && overflow != Overflow::clip; };
                    if (const Style* style = runtime.layout_style(node); style && (scrolls(style->overflow_x) || scrolls(style->overflow_y))) open.back().second.parts_apart = true;
                    if (std::string css = own_css(open.back().second); !css.empty()) painted.emplace(node.index, std::move(css));
                }
                open.pop_back();
                continue;
            }
            if (open.empty()) continue;
            Own& own = open.back().second;
            if (const auto* frame = std::get_if<mrender::NodeFrame>(&command)) {
                own.frame = frame->rect;
                own.moved = 0;
            } else if (std::holds_alternative<mrender::PushTransform>(command)) ++own.moved;
            else if (std::holds_alternative<mrender::PopTransform>(command)) --own.moved;
            else if (const auto* held = std::get_if<mrender::PushLayer>(&command); held && !own.frame && held->effect.id == 0) own.opacity *= held->opacity;  // the node whole, dimmed
            else if (const auto* glyphs = std::get_if<mrender::DrawGlyphs>(&command); glyphs && !glyphs->glyphs.empty()) {
                const NodeId node = runtime.node_at_index(open.back().first);
                if (!node.valid()) continue;
                own.parts_apart = true;
                Run run{mlibs::text::FontRef{glyphs->font.id}, glyphs->size, glyphs->paint.color, glyphs->glyphs.front().cluster, glyphs->glyphs.front().cluster, glyphs->glyphs.size(), {}, {}, {}};
                run.clusters.reserve(glyphs->glyphs.size());
                run.ids.reserve(glyphs->glyphs.size());
                for (const mrender::Glyph& glyph : glyphs->glyphs) {
                    run.begin = std::min(run.begin, glyph.cluster);
                    run.last = std::max(run.last, glyph.cluster);
                    run.clusters.push_back(glyph.cluster);
                    run.ids.push_back(glyph.id);
                }
                if (own.frame && own.moved == 0)
                    run.at = Point{glyphs->origin.x + glyphs->glyphs.front().position.x - own.frame->x, glyphs->origin.y + glyphs->glyphs.front().position.y - own.frame->y};
                drawn[node.index].push_back(std::move(run));
            } else if (!own.frame || own.moved != 0) {
                // Before its box is said, or under a transform of its own: not its own box's drawing.
            } else if (const auto* rect = std::get_if<mrender::FillRect>(&command)) {
                fills(own, rect->rect, nullptr, rect->paint);
                ground(open.back().first, own, rect->rect, {}, rect->paint);
            } else if (const auto* round = std::get_if<mrender::FillRoundRect>(&command)) {
                fills(own, round->rect, &round->radii, round->paint);
                ground(open.back().first, own, round->rect, round->radii, round->paint);
            } else if (const auto* stroke = std::get_if<mrender::StrokeRoundRect>(&command)) {
                // A border: a stroke centred half its width inside the box's edge is a ring along the inside.
                const float half = stroke->width / 2;
                const Rect outer{stroke->rect.x - half, stroke->rect.y - half, stroke->rect.width + stroke->width, stroke->rect.height + stroke->width};
                if (stroke->paint.kind == mrender::Paint::Kind::solid && stroke->paint.color.a > 0 && same_box(outer, *own.frame)) {
                    own.rings.push_back(std::format("inset 0 0 0 {} {}", px(std::max(stroke->width, 1.0f)), colour(stroke->paint.color)));
                    if (!own.radii && !stroke->radii.zero()) {
                        CornerRadii radii = stroke->radii;
                        for (float* corner : {&radii.top_left, &radii.top_right, &radii.bottom_right, &radii.bottom_left}) *corner += half;
                        own.radii = radii;
                    }
                }
            } else if (const auto* shadow = std::get_if<mrender::BoxShadow>(&command); shadow && shadow->color.a > 0 && same_box(shadow->rect, *own.frame)) {
                own.shadows.push_back(std::format("{}{} {} {} {} {}", shadow->inset ? "inset " : "", px(shadow->offset.x), px(shadow->offset.y), px(shadow->blur), px(shadow->spread), colour(shadow->color)));
                if (!own.radii && !shadow->radii.zero()) own.radii = shadow->radii;
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
    const auto in_row = [&](NodeId node) {
        const NodeId parent = runtime.parent(node);
        const Style* held = parent.valid() ? runtime.layout_style(parent) : nullptr;
        return held && held->display == mlibs::layout::Display::flex &&
               (held->direction == mlibs::layout::Direction::row || held->direction == mlibs::layout::Direction::row_reverse);
    };
    const auto font_of = [&](const Run& run) {
        const auto& face = fonts.face(fonts.face_of(run.font));
        Look::Font font{face.family, static_cast<int>(std::lround(fonts.weight_of(run.font))), fonts.slant_of(run.font) != mlibs::text::Slant::normal};
        if (!std::ranges::contains(look.fonts, font)) look.fonts.push_back(font);
        return font;
    };
    // An item's number in an ordered list: the list's start, and one more for each item before it.
    std::unordered_map<std::string_view, const spec::SemanticNode*> by_id, parent_of;
    for (const spec::SemanticNode& one : snapshot.nodes) {
        by_id.emplace(one.id, &one);
        for (const std::string& child : one.children) parent_of.emplace(child, &one);
    }
    const auto ordinal = [&](const spec::SemanticNode& item) -> std::optional<std::int64_t> {
        const auto up = parent_of.find(item.id);
        if (up == parent_of.end()) return std::nullopt;
        const spec::SemanticNode& list = *up->second;
        const Value* ordered = list.properties.get("ordered");
        if (list.role != Role::list || !ordered || !ordered->as_bool()) return std::nullopt;
        const Value* start = list.properties.get("start");
        std::int64_t number = start && start->is_number() ? start->as_int() : 1;
        for (const std::string& child : list.children) {
            if (child == item.id) return number;
            if (const auto found = by_id.find(child); found != by_id.end() && found->second->role == Role::list_item) ++number;
        }
        return std::nullopt;
    };
    for (const auto& [n, node] : nodes) {
        std::string css;
        if (const Style* style = runtime.layout_style(node)) css = layout_css(*style, in_stack(node), in_row(node));
        if (const auto own = painted.find(node.index); own != painted.end()) css += own->second;
        // The window's own ground, where the root drew none over it: what a frame is cleared to before anything is drawn.
        if (n == &snapshot.nodes.front() && !css.contains("background")) put(css, "background-color", colour(runtime.theme().color("background")));
        // A picture is as large as the engine set it — by its file's own size, which a browser reads otherwise (a
        // drawing with no size of its own fills the column) — and no wider than what holds it, its shape kept.
        if (n->role == Role::image) {
            if (const Rect box = runtime.bounds(node); box.width > 0 && box.height > 0) {
                put(css, "width", px(box.width));
                put(css, "height", "auto");
                put(css, "aspect-ratio", std::format("{}/{}", number(box.width), number(box.height)));
            }
        }
        if (const auto text = drawn.find(node.index); text != drawn.end()) {
            // What holds text sets it in lines, as a block does: its runs and the links in them flow together. A node
            // that holds more than its text places (a list's item: its paragraph and the list inside it, beside the
            // number it drew) keeps the layout that sets them apart.
            const bool holds_blocks = std::ranges::any_of(n->children, [&](const std::string& child) {
                return std::ranges::none_of(n->marks, [&](const spec::Mark& mark) { return mark.node == child; });
            });
            if (!holds_blocks && css.starts_with("display:flex;")) css.replace(0, 13, "display:block;");
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
            // The character a glyph's cluster begins with, and its bytes (none where the node's text is not the text
            // drawn: a control that draws its label, not its name).
            const auto character_at = [&](std::uint32_t cluster) -> std::optional<mlibs::unicode::Decoded> {
                if (cluster >= n->name.size()) return std::nullopt;
                return mlibs::unicode::decode(n->name, cluster);
            };
            // The faces the runs of one setting (a size, a weight, a slant) were drawn in, in the order the engine
            // tried them — the faces its style names, then the fallbacks: one face stands before another where it drew
            // a character the other has (else the other would have drawn it). A browser that tries them in that order,
            // character by character, sets each where the frame did: the Latin words of a Chinese paragraph in the
            // Latin face, not in the Chinese face's own Latin letters (3 px along a line). White space says nothing of
            // the order (the engine sets it beside what it follows; below). A face that cannot stand in one order with
            // the run's own (each has what the other drew: code at the size of the prose around it) is not among them:
            // its runs are parts of their own.
            const auto setting = [&](const Run& run) { return std::tuple{run.size, fonts.weight_of(run.font), fonts.slant_of(run.font)}; };
            const auto faces_in_order = [&](const Run& lead) {
                struct Drawn {
                    mlibs::text::FaceId   face = 0;
                    std::size_t           glyphs = 0;
                    std::vector<char32_t> characters;  // each once; only those the face has (what it drew of the text)
                };
                std::vector<Drawn> all{{fonts.face_of(lead.font), 0, {}}};
                for (const Run& run : runs) {
                    if (setting(run) != setting(lead)) continue;
                    const mlibs::text::FaceId face = fonts.face_of(run.font);
                    auto found = std::ranges::find(all, face, &Drawn::face);
                    if (found == all.end()) found = all.insert(all.end(), Drawn{face, 0, {}});
                    found->glyphs += run.glyphs;
                    for (const std::uint32_t cluster : run.clusters) {
                        const auto character = character_at(cluster);
                        if (character && !white(character->cp) && fonts.covers(face, character->cp) && !std::ranges::contains(found->characters, character->cp))
                            found->characters.push_back(character->cp);
                    }
                }
                const auto before = [&](const Drawn& first, const Drawn& then) {
                    return std::ranges::any_of(first.characters, [&](char32_t character) { return fonts.covers(then.face, character); });
                };
                std::stable_sort(all.begin() + 1, all.end(), [](const Drawn& one, const Drawn& other) { return one.glyphs > other.glyphs; });
                std::vector<const Drawn*> left;
                for (const Drawn& drawn : all)
                    if (std::ranges::none_of(left, [&](const Drawn* kept) { return before(drawn, *kept) && before(*kept, drawn); })) left.push_back(&drawn);
                std::vector<mlibs::text::FaceId> order;
                while (!left.empty()) {
                    auto next = std::ranges::find_if(left, [&](const Drawn* one) {
                        return std::ranges::none_of(left, [&](const Drawn* other) { return other != one && before(*other, *one); });
                    });
                    if (next == left.end()) next = left.begin();  // three that each stand before the next: as drawn most
                    order.push_back((*next)->face);
                    left.erase(next);
                }
                return order;
            };
            const auto families = [&](std::span<const mlibs::text::FaceId> faces) {
                std::string value;
                for (const mlibs::text::FaceId face : faces) value += std::format("'{}',", font_key(fonts.face(face).family));
                return value + "system-ui,sans-serif";
            };
            const std::vector<mlibs::text::FaceId> own_faces = faces_in_order(*own);
            put(css, "font-family", families(own_faces));
            put(css, "font-size", px(own->size));
            put(css, "font-weight", std::format("{}", font.weight));
            if (font.italic) put(css, "font-style", "italic");
            put(css, "color", colour(own->color));
            // Its lines as the frame set them: one line's height.
            const auto lines = runtime.text_lines(node);
            if (!lines.empty() && lines.front().bounds.height > 0) put(css, "line-height", px(lines.front().bounds.height));
            // A control (a link, a button) measures itself whole: its words on one line, its box its own — a whole
            // pixel at least, room for its underline or its ring — whatever the words alone would take. The box's
            // height is the least it is; its width too where the box is its words' (not one a column stretched).
            if (n->role == Role::link || n->role == Role::button) {
                const Rect box = runtime.bounds(node);
                css += "white-space:nowrap;";
                put(css, "min-height", px(box.height));
                if (!lines.empty() && box.width - lines.front().bounds.width < 2) put(css, "min-width", px(box.width));
            }
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
            // The run each ground lies under: of the runs whose first glyph stands in it, the one nearest its left edge
            // (the text after inline code begins inside the code's ground too, where the ground runs on past the code).
            // Shapes of one ground that overlap along a line (the frame drew the code's ground in two pieces, each a
            // little wider than its glyphs) are one.
            std::vector<const Ground*> grounded(runs.size(), nullptr);
            if (auto found = grounds.find(node.index); found != grounds.end()) {
                std::vector<Ground>& all = found->second;
                std::ranges::sort(all, [](const Ground& one, const Ground& other) { return std::pair{one.rect.y, one.rect.x} < std::pair{other.rect.y, other.rect.x}; });
                std::vector<Ground> merged;
                for (const Ground& one : all) {
                    Ground* last = merged.empty() ? nullptr : &merged.back();
                    if (last && about(last->rect.y, one.rect.y) && about(last->rect.height, one.rect.height) && one.rect.x <= last->rect.x + last->rect.width &&
                        last->color.r == one.color.r && last->color.g == one.color.g && last->color.b == one.color.b && last->color.a == one.color.a)
                        last->rect.width = std::max(last->rect.x + last->rect.width, one.rect.x + one.rect.width) - last->rect.x;
                    else merged.push_back(one);
                }
                all = std::move(merged);
                for (const Ground& one : all) {
                    std::optional<std::size_t> nearest;
                    for (std::size_t at = 0; at < runs.size(); ++at) {
                        const std::optional<Point>& start = runs[at].at;
                        if (!start || start->x < one.rect.x || start->x > one.rect.x + one.rect.width || start->y < one.rect.y || start->y > one.rect.y + one.rect.height) continue;
                        if (!nearest || start->x < runs[*nearest].at->x) nearest = at;
                    }
                    if (nearest && !grounded[*nearest]) grounded[*nearest] = &one;
                }
            }
            std::vector<Look::Span> parts, apart;
            for (const Run& run : runs) {
                const Look::Font other = font_of(run);  // a face the page declares, the node's own faces' too
                const mlibs::text::FaceId face = fonts.face_of(run.font);
                const bool among_own = setting(run) == setting(*own) && std::ranges::contains(own_faces, face);
                const std::vector<mlibs::text::FaceId> faces = among_own ? own_faces : faces_in_order(run);
                // White space the frame set in another face than the page would — the first of the faces it is set in
                // that has it — stands apart in its own: the engine sets a space beside what it follows (after Chinese
                // text, the Chinese face's 3.4 px space), a browser by the faces' order (the Latin face's 4.2 px), and
                // every word after it on the line would go 0.8 px further.
                for (const std::uint32_t cluster : run.clusters) {
                    const auto character = character_at(cluster);
                    if (!character || !white(character->cp)) continue;
                    const auto given = std::ranges::find_if(faces, [&](mlibs::text::FaceId one) { return fonts.covers(one, character->cp); });
                    if (given == faces.end() || *given == face) continue;
                    std::string style;
                    put(style, "font-family", families(std::span(&face, 1)));
                    put(style, "line-height", "0");  // its face's ascent adds no height to the line (as a part's below)
                    if (!apart.empty() && apart.back().end == cluster && apart.back().style == style) apart.back().end += character->length;
                    else apart.push_back({cluster, cluster + character->length, std::move(style)});
                }
                // The ground the node drew under the run (inline code's tinted, rounded rectangle, from a line's top to its
                // bottom less a pixel, a little wider than the glyphs): the part's element's own background, as wide by
                // a padding its negative margin takes back and as tall by a padding above and below its face's ascent
                // and descent — a ground that moves no word. Each line's piece of it is one (`box-decoration-break`).
                const Ground* under = grounded[static_cast<std::size_t>(&run - runs.data())];
                if (among_own && run.same_colour(*own) && !under) continue;
                std::string differs;
                if (under) {
                    const mlibs::text::Metrics metrics = fonts.metrics(run.font, run.size);
                    const float side = std::max(0.0f, run.at->x - under->rect.x);
                    const float above = std::max(0.0f, run.at->y - under->rect.y - metrics.ascent), below = std::max(0.0f, under->rect.y + under->rect.height - run.at->y - metrics.descent);
                    put(differs, "background-color", colour(under->color));
                    if (!under->radii.zero()) put(differs, "border-radius", radii_css(under->radii));
                    put(differs, "padding", std::format("{} {} {} {}", px(above), px(side), px(below), px(side)));
                    if (side > 0) put(differs, "margin", std::format("0 {}", px(-side)));
                    put(differs, "-webkit-box-decoration-break", "clone");
                    put(differs, "box-decoration-break", "clone");
                }
                if (faces != own_faces) put(differs, "font-family", families(faces));
                if (run.size != own->size) put(differs, "font-size", px(run.size));
                // A part in another face or size stands on the line its node set: in CSS its own ascent and descent
                // would make that line taller (a line with code in it 2 px more), so its box adds no height of its own.
                if (faces.front() != own_faces.front() || run.size != own->size) put(differs, "line-height", "0");
                if (other.weight != font.weight) put(differs, "font-weight", std::format("{}", other.weight));
                if (other.italic != font.italic) put(differs, "font-style", other.italic ? "italic" : "normal");
                if (!run.same_colour(*own)) put(differs, "color", colour(run.color));
                if (!differs.empty()) parts.push_back({run.begin, run.last + 1, std::move(differs)});
            }
            if (!parts.empty()) look.spans.emplace(n->id, std::move(parts));
            if (!apart.empty()) {
                std::ranges::sort(apart, {}, &Look::Span::begin);
                look.apart.emplace(n->id, std::move(apart));
            }
            // An item of an ordered list draws its number beside what it holds; no element of the page holds that
            // number (a browser's own markers are its own, and the look's sheet sets them none). The page draws the
            // same number in the same face where the frame drew it: a box generated before the item (the sheet's
            // `li::before`), said by custom properties — no word is added to the document, so finding, selecting and
            // reading it back stay as they were. Said only where the glyphs drawn are that number's, glyph for glyph.
            if (n->role == Role::list_item && runs.size() == 1 && own->at)
                if (const auto number = ordinal(*n)) {
                    // The number's text and figures as drawn: each way it may be written, set in the run's face, and
                    // kept where its glyphs are the run's.
                    const auto drawn_as = [&](std::string said, bool tabular) -> std::optional<std::pair<std::string, bool>> {
                        mlibs::text::ParagraphStyle plain;
                        plain.style.families = {fonts.face(fonts.face_of(own->font)).family};
                        plain.style.size = own->size;
                        plain.style.weight = fonts.weight_of(own->font);
                        plain.style.slant = fonts.slant_of(own->font);
                        if (tabular) plain.style.features.push_back(mlibs::text::Feature::on("tnum"));
                        plain.wrap = false;
                        mlibs::text::Paragraph set(fonts, said, plain);
                        set.layout(std::numeric_limits<float>::infinity());
                        std::vector<std::uint32_t> ids;
                        for (const mlibs::text::Line& line : set.lines())
                            for (const mlibs::text::GlyphRun& glyphs : line.runs)
                                for (const mlibs::text::Glyph& glyph : glyphs.glyphs) ids.push_back(glyph.id);
                        if (ids != own->ids) return std::nullopt;
                        return std::pair{std::move(said), tabular};
                    };
                    std::optional<std::pair<std::string, bool>> marker;
                    for (const char delimiter : {'.', ')'})
                        for (const bool tabular : {true, false})
                            if (!marker) marker = drawn_as(std::format("{}{}", *number, delimiter), tabular);
                    if (marker) {
                        const mlibs::text::Metrics metrics = fonts.metrics(own->font, own->size);
                        put(css, "--omui-marker", std::format("\"{}\"", marker->first));
                        put(css, "--omui-marker-left", px(own->at->x));
                        put(css, "--omui-marker-top", px(own->at->y - metrics.ascent));
                        put(css, "--omui-marker-font", std::format("{} {}/{} {}", font.weight, px(own->size), px(metrics.ascent + metrics.descent), families(own_faces)));
                        put(css, "--omui-marker-color", colour(own->color));
                        if (marker->second) put(css, "--omui-marker-numeric", "tabular-nums");
                    }
                }
        }
        // A field holds no words a page sets — what it shows (its value, its placeholder) is drawn in it — so a
        // block of its style alone would be as short as its least height: it is as tall as the frame set it.
        if (n->role == Role::text_box)
            if (const Rect box = runtime.bounds(node); box.height > 0) put(css, "min-height", px(box.height));
        if (!css.empty()) look.styles.emplace(n->id, std::move(css));
        // The nodes of the view between it and the nearest one the semantic tree states.
        std::vector<Look::Wrapper> around;
        for (NodeId up = runtime.parent(node); up.valid() && !stated.contains(up.index); up = runtime.parent(up)) {
            const Style* style = runtime.layout_style(up);
            std::string css = style ? layout_css(*style, in_stack(up), in_row(up)) : std::string();
            if (const auto own = painted.find(up.index); own != painted.end()) css += own->second;
            around.push_back({up.index, std::move(css)});
        }
        if (!around.empty()) {
            std::ranges::reverse(around);
            look.wrappers.emplace(n->id, std::move(around));
        }
    }
    return look;
}

std::string look_sheet(const Look& look) {
    std::string sheet =
        // The engine sets a glyph where its outline's own advance puts it; a browser that rounds each advance to a
        // whole pixel (Chromium on Linux with full hinting: a line of 16 px text 5 px shorter, of 13 px code 4 %
        // longer) is asked for the outline's geometry too — a control included, which the browser's own sheet resets.
        // The engine puts an eighth of an em between an ideograph and a Latin letter or digit (mlibs.text's autospace,
        // CSS Text 4's `normal`); a browser does so only when asked (Chromium's and Firefox's own value is none).
        "[data-omui-look]{box-sizing:border-box;margin:0;font:16px/1.5 system-ui,sans-serif;font-optical-sizing:none;text-rendering:geometricPrecision;text-autospace:normal}"
        "[data-omui-look] *{box-sizing:border-box;margin:0;padding:0;border:0;list-style:none;font:inherit;text-rendering:inherit;color:inherit;background:none;scrollbar-width:none;text-decoration:none}"
        "[data-omui-look] ::-webkit-scrollbar{display:none}[data-omui-look] a[href]{text-decoration:underline}"
        "[data-omui-look] strong,[data-omui-look] b{font-weight:700}[data-omui-look] em,[data-omui-look] i{font-style:italic}"
        // A table's heading stands where its column does: at the left unless the column says (a browser's own sheet centres it).
        "[data-omui-look] th:not([align]){text-align:left}"
        // Struck text as the frame strikes it (the rule above takes every element's decoration away).
        "[data-omui-look] del,[data-omui-look] s{text-decoration:line-through}"
        // An ordered list's item's number, where the frame drew it and as (its declarations: `html_look`) — only on the
        // item that says it: an item inside it inherits the declarations, not the number.
        "[data-omui-look] li[style*='--omui-marker:']::before{content:var(--omui-marker);position:absolute;left:var(--omui-marker-left);top:var(--omui-marker-top);"
        "font:var(--omui-marker-font);color:var(--omui-marker-color);font-variant-numeric:var(--omui-marker-numeric,normal);white-space:pre}"
        // On paper the document is whole: nothing of it stays inside a view that scrolls or a box of the window's size.
        "@media print{[data-omui-look],[data-omui-look] *{overflow:visible!important;height:auto!important;max-height:none!important}}";
    if (!look.dark.empty()) {
        // Over the light values the root states itself.
        std::string values;
        for (std::string_view rest = look.dark; !rest.empty();) {
            const auto end = rest.find(';');
            if (const std::string_view one = rest.substr(0, end); !one.empty()) values += std::format("{}!important;", one);
            if (end == std::string_view::npos) break;
            rest.remove_prefix(end + 1);
        }
        sheet += std::format("@media (prefers-color-scheme:dark){{[data-omui-look]{{{}}}}}", values);
    }
    return sheet;
}

Look by_schemes(Look light, const Look& dark) {
    std::vector<std::pair<std::string, std::string>> names;
    for (auto& [id, css] : light.styles)
        if (const auto other = dark.styles.find(id); other != dark.styles.end()) css = by_scheme(css, other->second, names);
    for (auto& [id, around] : light.wrappers)
        if (const auto other = dark.wrappers.find(id); other != dark.wrappers.end() && other->second.size() == around.size())
            for (std::size_t at = 0; at < around.size(); ++at) around[at].style = by_scheme(around[at].style, other->second[at].style, names);
    for (auto& [id, parts] : light.spans)
        if (const auto other = dark.spans.find(id); other != dark.spans.end() && other->second.size() == parts.size())
            for (std::size_t at = 0; at < parts.size(); ++at) parts[at].style = by_scheme(parts[at].style, other->second[at].style, names);
    for (std::size_t at = 0; at < names.size(); ++at) {
        light.light += std::format("--o{}:{};", at, names[at].first);
        light.dark += std::format("--o{}:{};", at, names[at].second);
    }
    return light;
}

}  // namespace omui::semantic
