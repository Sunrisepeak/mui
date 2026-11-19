module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

namespace {

constexpr float playhead_diameter = 16, mark_diameter = 7, track_width = 3;
constexpr float line_width = 240, line_height = 28, shape_side = 160;

// The shapes it has by name, in a 1×1 box: a line across the middle; a ring from the top, clockwise.
constexpr std::string_view line_data = "M0 0.5 L1 0.5";
constexpr std::string_view ring_data = "M0.5 0 A0.5 0.5 0 1 1 0.5 1 A0.5 0.5 0 1 1 0.5 0 Z";

}  // namespace

void Timeline::mounted() { size_ = {}; }

void Timeline::prop_changed(TimelineProp p) {
    if (!attached()) return;
    if (p == TimelineProp::shape) course_shape_.clear();  // laid again
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size Timeline::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    if (props.shape == "line") return {width.value_or(aw.is_definite() ? std::min(aw.value, line_width) : line_width), height.value_or(line_height)};
    return {width.value_or(shape_side), height.value_or(shape_side)};
}

const Timeline::Course& Timeline::course(Size size) const {
    if (course_size_ == size && course_shape_ == props.shape && !course_.points.empty()) return course_;
    course_ = {};
    course_size_ = size;
    course_shape_ = props.shape;
    const std::string_view text = props.shape == "line" ? line_data : props.shape == "ring" ? ring_data : std::string_view(props.shape);
    auto data = mlibs::geom::parse_path_data(text);
    if (!data || data->verbs.empty()) data = mlibs::geom::parse_path_data(line_data);  // not a path: a line, as it is by default
    // Laid inside its box, the playhead's half kept clear at every side.
    const float inset = playhead_diameter / 2 + 2;
    const Rect box{inset, inset, std::max(1.0f, size.width - 2 * inset), std::max(1.0f, size.height - 2 * inset)};
    mlibs::geom::PathPolylines lines;
    mlibs::geom::flatten_path(data->view(), mlibs::geom::Affine::translate(box.x, box.y) * mlibs::geom::Affine::scale(box.width, box.height), lines, 0.25f);
    if (lines.count() == 0) return course_;
    const auto first = lines.polygon(0);  // its first contour
    course_.points.assign(first.begin(), first.end());
    course_.closed = lines.closed[0] || props.shape == "ring";
    if (course_.closed && !course_.points.empty() && (course_.points.back().x != course_.points.front().x || course_.points.back().y != course_.points.front().y))
        course_.points.push_back(course_.points.front());
    float along = 0;
    for (std::size_t index = 0; index < course_.points.size(); ++index) {
        if (index > 0) {
            const Point from = course_.points[index - 1], to = course_.points[index];
            along += std::hypot(to.x - from.x, to.y - from.y);
        }
        course_.lengths.push_back(along);
    }
    return course_;
}

float Timeline::fraction_of(double step) const noexcept {
    const auto steps = static_cast<double>(count());
    const double at = std::clamp(step, 1.0, steps) - 1;
    // Open, the last step at the end; closed, a step before the first again.
    return static_cast<float>(course_.closed ? at / steps : at / (steps - 1));
}

Point Timeline::point_at(double step, Size size) const {
    const Course& laid = course(size);
    if (laid.points.empty()) return {size.width / 2, size.height / 2};
    const float target = fraction_of(step) * laid.total();
    const auto after = std::ranges::lower_bound(laid.lengths, target);
    if (after == laid.lengths.begin()) return laid.points.front();
    if (after == laid.lengths.end()) return laid.points.back();
    const auto index = static_cast<std::size_t>(after - laid.lengths.begin());
    const float span = laid.lengths[index] - laid.lengths[index - 1];
    const float part = span > 0 ? (target - laid.lengths[index - 1]) / span : 0;
    const Point from = laid.points[index - 1], to = laid.points[index];
    return {from.x + (to.x - from.x) * part, from.y + (to.y - from.y) * part};
}

double Timeline::step_at(Point local, Size size) const {
    const Course& laid = course(size);
    if (laid.points.size() < 2 || laid.total() <= 0) return props.value;
    // The shape's nearest point to it, and its length along.
    float nearest = std::numeric_limits<float>::infinity(), along = 0;
    for (std::size_t index = 1; index < laid.points.size(); ++index) {
        const Point from = laid.points[index - 1], to = laid.points[index];
        const float dx = to.x - from.x, dy = to.y - from.y, length_squared = dx * dx + dy * dy;
        const float part = length_squared > 0 ? std::clamp(((local.x - from.x) * dx + (local.y - from.y) * dy) / length_squared, 0.0f, 1.0f) : 0.0f;
        const float x = from.x + dx * part - local.x, y = from.y + dy * part - local.y, distance = x * x + y * y;
        if (distance < nearest) nearest = distance, along = laid.lengths[index - 1] + std::sqrt(length_squared) * part;
    }
    const auto steps = static_cast<double>(count());
    const double fraction = along / laid.total();
    double step = 1 + std::round(fraction * (laid.closed ? steps : steps - 1));
    if (laid.closed && step > steps) step = 1;  // past the last, round to the first again
    return std::clamp(step, 1.0, steps);
}

bool Timeline::change(double step) {
    const double to = std::clamp(std::round(step), 1.0, static_cast<double>(count()));
    if (to == props.value) return false;
    write_value(to);
    emit_changed(to);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return true;
}

void Timeline::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    size_ = ctx.size;
    const Course& laid = course(ctx.size);
    if (laid.points.size() < 2) return;
    const bool off = props.disabled || ctx.state.disabled;
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});
    xrender::Path path;
    path.move_to(laid.points.front());
    for (std::size_t index = 1; index < laid.points.size(); ++index) path.line_to(laid.points[index]);
    if (laid.closed) path.close();
    const Rect whole{0, 0, ctx.size.width, ctx.size.height};
    ctx.part({.role = "track", .allocation = whole}, [&] {
        ctx.list.stroke_path(path, {.width = track_width, .cap = xrender::LineCap::round, .join = xrender::LineJoin::round}, xrender::Paint::solid(th.color("track")));
    });
    const float played = fraction_of(props.value);
    if (played > 0)
        ctx.part({.role = "played", .allocation = whole}, [&] {
            ctx.list.stroke_path(path, {.width = track_width, .cap = xrender::LineCap::round, .join = xrender::LineJoin::round, .trim_end = played},
                                 xrender::Paint::solid(th.color("accent")));
        });
    // Each step's mark, its id its number: reached ones in the accent.
    for (std::int64_t step = 1; step <= count(); ++step) {
        const Point at = point_at(static_cast<double>(step), ctx.size);
        const std::string id = std::to_string(step);
        const Rect mark{at.x - mark_diameter / 2, at.y - mark_diameter / 2, mark_diameter, mark_diameter};
        ctx.part({.role = "mark", .id = id, .allocation = mark, .kind = xrender::PartKind::box}, [&] {
            ctx.list.fill_round_rect(mark, CornerRadii::all(mark_diameter / 2),
                                     xrender::Paint::solid(static_cast<double>(step) <= props.value ? th.color("accent") : th.color("track")));
        });
    }
    // The playhead at the step it is at; its focus ring when the keys reach it. On a line it is one thing carried
    // along it; on any other shape a straight carry would cut across the shape, so each step's playhead is its own
    // part (its id the step): it goes out where it was and comes in where it is, while the part played grows round.
    const Point head = point_at(props.value, ctx.size);
    const Rect playhead{head.x - playhead_diameter / 2, head.y - playhead_diameter / 2, playhead_diameter, playhead_diameter};
    const std::string head_id = props.shape == "line" ? std::string() : std::to_string(std::lround(props.value));
    ctx.part({.role = "playhead", .id = head_id, .allocation = playhead, .kind = xrender::PartKind::box}, [&] {
        const CornerRadii round = CornerRadii::all(playhead_diameter / 2);
        ctx.list.fill_round_rect(playhead, round, xrender::Paint::solid(th.color("thumb")));
        ctx.list.stroke_round_rect({playhead.x + 1, playhead.y + 1, playhead_diameter - 2, playhead_diameter - 2}, CornerRadii::all(playhead_diameter / 2 - 1), 2,
                                   xrender::Paint::solid(th.color("accent")));
    });
    if (!off && !dragging_ && ctx.state.focused && ctx.state.focus_visible) parts::focus_ring(ctx, playhead, CornerRadii::all(playhead_diameter / 2));
    if (off) ctx.list.pop_layer();
}

bool Timeline::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* pointer = std::get_if<PointerEvent>(&e);
    if (!pointer || props.disabled) return false;
    const Size size = size_.width > 0 ? size_ : services().bounds(node()).size();
    switch (pointer->kind) {
        case PointerKind::down:
            if (pointer->button != MouseButton::left && pointer->type == PointerType::mouse) return false;
            dragging_ = true;
            drag_from_ = props.value;
            services().capture_pointer(node());
            change(step_at(ctx.local, size));
            return true;
        case PointerKind::move:
            if (!dragging_) return false;
            change(step_at(ctx.local, size));
            return true;
        case PointerKind::up:
        case PointerKind::cancel:
            if (!dragging_) return false;
            dragging_ = false;
            services().release_pointer();
            // The drag as one action of the person's (S-12): undo takes it back whole.
            if (props.value != drag_from_)
                services().performed(node(), "set_value", Value{}.set("value", props.value), {{"set_value", Value{}.set("value", drag_from_)}});
            services().invalidate_paint(node());
            return true;
        default:
            return false;
    }
}

void Timeline::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    const auto at = static_cast<std::size_t>(std::clamp(props.value, 1.0, static_cast<double>(count())));
    // What the step is, in its words or by its number.
    node.description = at <= props.captions.size() ? props.captions[at - 1] : std::format("Step {} of {}", at, count());
    node.value = Value{}
                     .set("value", props.value)
                     .set("min", 1.0)
                     .set("max", static_cast<double>(count()))
                     .set("step", 1.0)
                     .set("shape", props.shape == "line" || props.shape == "ring" ? props.shape : std::string("path"));
}

// Each move is taken back by setting the step it replaced (S-12); one that moves nothing leaves no step to take back.
ActResult Timeline::increment(ActContext&) {
    const double was = props.value;
    if (!change(props.value + 1)) return ActResult::done(Value(props.value));
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", was));
}

ActResult Timeline::decrement(ActContext&) {
    const double was = props.value;
    if (!change(props.value - 1)) return ActResult::done(Value(props.value));
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", was));
}

ActResult Timeline::set_value(ActContext&, const Value& value) {
    const double was = props.value;
    double to = props.value;
    if (value.is_number()) to = value.as_double();
    else return ActResult::fail(std::format("set_value takes a step's number (1 … {}), not {}", count(), value.json()));
    if (!change(to)) return ActResult::done(Value(props.value));
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", was));
}

}  // namespace omui
