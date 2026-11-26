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
                Run run{mlibs::text::FontRef{glyphs->font.id}, glyphs->size, glyphs->paint.color, glyphs->glyphs.front().cluster, glyphs->glyphs.front().cluster, glyphs->glyphs.size()};
                for (const mrender::Glyph& glyph : glyphs->glyphs) {
                    run.begin = std::min(run.begin, glyph.cluster);
                    run.last = std::max(run.last, glyph.cluster);
                }
                drawn[node.index].push_back(run);
            } else if (!own.frame || own.moved != 0) {
                // Before its box is said, or under a transform of its own: not its own box's drawing.
            } else if (const auto* rect = std::get_if<mrender::FillRect>(&command)) fills(own, rect->rect, nullptr, rect->paint);
            else if (const auto* round = std::get_if<mrender::FillRoundRect>(&command)) fills(own, round->rect, &round->radii, round->paint);
            else if (const auto* stroke = std::get_if<mrender::StrokeRoundRect>(&command)) {
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
            put(css, "font-family", std::format("'{}',system-ui,sans-serif", font_key(font.family)));
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
            std::vector<Look::Span> parts;
            for (const Run& run : runs) {
                if (run.same_as(*own)) continue;
                const Look::Font other = font_of(run);
                std::string differs;
                if (other.family != font.family) put(differs, "font-family", std::format("'{}',system-ui,sans-serif", font_key(other.family)));
                if (run.size != own->size) put(differs, "font-size", px(run.size));
                // A part in another face or size stands on the line its node set: in CSS its own ascent and descent
                // would make that line taller (a line with code in it 2 px more), so its box adds no height of its own.
                if (other.family != font.family || run.size != own->size) put(differs, "line-height", "0");
                if (other.weight != font.weight) put(differs, "font-weight", std::format("{}", other.weight));
                if (other.italic != font.italic) put(differs, "font-style", other.italic ? "italic" : "normal");
                if (!run.same_colour(*own)) put(differs, "color", colour(run.color));
                if (!differs.empty()) parts.push_back({run.begin, run.last + 1, std::move(differs)});
            }
            if (!parts.empty()) look.spans.emplace(n->id, std::move(parts));
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
        "[data-omui-look]{box-sizing:border-box;margin:0;font:16px/1.5 system-ui,sans-serif;font-optical-sizing:none;text-rendering:geometricPrecision}"
        "[data-omui-look] *{box-sizing:border-box;margin:0;padding:0;border:0;list-style:none;font:inherit;text-rendering:inherit;color:inherit;background:none;scrollbar-width:none;text-decoration:none}"
        "[data-omui-look] ::-webkit-scrollbar{display:none}[data-omui-look] a[href]{text-decoration:underline}"
        "[data-omui-look] strong,[data-omui-look] b{font-weight:700}[data-omui-look] em,[data-omui-look] i{font-style:italic}"
        // A table's heading stands where its column does: at the left unless the column says (a browser's own sheet centres it).
        "[data-omui-look] th:not([align]){text-align:left}"
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
