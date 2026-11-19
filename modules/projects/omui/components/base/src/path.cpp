module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

namespace {

constexpr float default_side = 24;

}  // namespace

void Path::prop_changed(PathProp p) {
    if (!attached()) return;
    if (p == PathProp::label) services().invalidate_semantics(node());
    services().invalidate_paint(node());
    if (p == PathProp::view_box) services().invalidate_layout(node());
}

Rect Path::view_box() const noexcept {
    std::array<float, 4> numbers{};
    std::size_t found = 0;
    std::string_view rest = props.view_box;
    while (found < numbers.size() && !rest.empty()) {
        const auto start = rest.find_first_not_of(" ,\t");
        if (start == std::string_view::npos) break;
        rest.remove_prefix(start);
        const auto [end, failure] = std::from_chars(rest.data(), rest.data() + rest.size(), numbers[found]);
        if (failure != std::errc{}) break;
        rest.remove_prefix(static_cast<std::size_t>(end - rest.data()));
        ++found;
    }
    if (found != 4 || !(numbers[2] > 0) || !(numbers[3] > 0)) return {0, 0, 100, 100};
    return {numbers[0], numbers[1], numbers[2], numbers[3]};
}

const xrender::Path* Path::shape() const {
    if (shape_of_ == props.d && shape_) return &*shape_;
    shape_of_ = props.d;
    shape_.reset();
    const auto data = mlibs::geom::parse_path_data(props.d);
    if (!data || data->verbs.empty()) return nullptr;
    xrender::Path made;
    std::size_t at = 0;
    for (const mlibs::geom::PathVerb verb : data->verbs) {
        switch (verb) {
            case mlibs::geom::PathVerb::move: made.move_to(data->points[at++]); break;
            case mlibs::geom::PathVerb::line: made.line_to(data->points[at++]); break;
            case mlibs::geom::PathVerb::quad:
                made.quad_to(data->points[at], data->points[at + 1]);
                at += 2;
                break;
            case mlibs::geom::PathVerb::cubic:
                made.cubic_to(data->points[at], data->points[at + 1], data->points[at + 2]);
                at += 3;
                break;
            case mlibs::geom::PathVerb::close: made.close(); break;
        }
    }
    shape_ = std::move(made);
    return &*shape_;
}

bool Path::drawable() const { return shape() != nullptr; }

Size Path::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    // Unsized, its view box's ratio at the default height; one side given, the other by the ratio.
    const Rect box = view_box();
    const float ratio = box.width / box.height;
    if (width && height) return {*width, *height};
    if (width) return {*width, *width / ratio};
    if (height) return {*height * ratio, *height};
    return {default_side * ratio, default_side};
}

void Path::paint(PaintContext& ctx) const {
    const xrender::Path* drawn = shape();
    if (!drawn || ctx.content.empty()) return;
    // The view box into the content box, its ratio kept, centred.
    const Rect box = view_box();
    const float scale = std::min(ctx.content.width / box.width, ctx.content.height / box.height);
    const float left = ctx.content.x + (ctx.content.width - box.width * scale) / 2 - box.x * scale;
    const float top = ctx.content.y + (ctx.content.height - box.height * scale) / 2 - box.y * scale;
    const xrender::Path placed = drawn->transformed(mlibs::geom::Affine::translate(left, top) * mlibs::geom::Affine::scale(scale, scale));
    const Theme& th = ctx.theme;
    ctx.part({.role = "shape", .allocation = {0, 0, ctx.size.width, ctx.size.height}}, [&] {
        if (!props.fill.empty()) ctx.list.fill_path(placed, xrender::FillRule::nonzero, xrender::Paint::solid(th.color(props.fill)));
        const float trim = static_cast<float>(std::clamp(props.trim, 0.0, 1.0));
        if (!props.stroke.empty() && props.stroke_width > 0 && trim > 0)
            ctx.list.stroke_path(placed,
                                 {.width = static_cast<float>(props.stroke_width) * scale, .cap = xrender::LineCap::round,
                                  .join = xrender::LineJoin::round, .trim_end = trim},
                                 xrender::Paint::solid(th.color(props.stroke)));
    });
}

void Path::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
}

}  // namespace omui
