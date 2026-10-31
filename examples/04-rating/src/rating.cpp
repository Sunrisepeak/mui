// The rating's behaviour: stars that fill to the rating (gliding, on the
// theme's spring), a preview of what a click gives where the pointer is, and
// the actions the contract declares — which the keys and agents use alike.
module shelf;

import std;
import openmui.core;
import openmui.view;
import openmui.ui.base;

namespace shelf {

namespace {

constexpr float star = 22, gap = 4;

// A five-pointed star in a square.
omui::render::Path star_path(omui::Rect r) {
    omui::render::Path p;
    const omui::Point c{r.x + r.width / 2, r.y + r.height / 2 + r.height * 0.04f};
    const float outer = r.width / 2, inner = outer * 0.48f;
    for (int i = 0; i < 10; ++i) {
        const double a = -std::numbers::pi / 2 + i * std::numbers::pi / 5;
        const float rad = i % 2 ? inner : outer;
        const omui::Point q{c.x + rad * static_cast<float>(std::cos(a)), c.y + rad * static_cast<float>(std::sin(a))};
        i ? p.line_to(q) : p.move_to(q);
    }
    return p.close();
}

}  // namespace

double Rating::snap(double v) const noexcept {
    const double s = step();
    return std::clamp(std::round(v / s) * s, 0.0, static_cast<double>(std::max(props.max, 1)));
}

double Rating::at(float x) const noexcept {
    // The star under x, whole or (half stars) its left half.
    const float pitch = star + gap;
    const int i = static_cast<int>(std::floor(x / pitch));
    const float within = x - i * pitch;
    double v = i + 1.0;
    if (props.half && within < star / 2) v -= 0.5;
    return snap(v);
}

void Rating::mounted() {
    fill_.attach(services(), props.value);
    hover_.attach(services(), 0);
}

void Rating::prop_changed(RatingProp p) {
    if (!attached()) return;
    if (p == RatingProp::value) fill_.to(snap(props.value), services().theme().transition("motion.spring.snappy"));
    services().invalidate_paint(node());
    if (p == RatingProp::max) services().invalidate_layout(node());
}

omui::Size Rating::measure(omui::MeasureContext&, std::optional<float> width, std::optional<float> height, omui::layout::Available,
                           omui::layout::Available) {
    const int n = std::max(props.max, 1);
    return {width.value_or(n * star + (n - 1) * gap), height.value_or(star)};
}

void Rating::paint(omui::PaintContext& ctx) const {
    const omui::Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    hover_.to(!off && !props.readonly && preview_ >= 0 ? 1.0 : 0.0, th.transition("motion.fast"));
    const double shown = std::clamp(fill_.value(), 0.0, static_cast<double>(props.max));
    const omui::Color on = off ? th.color("text_disabled") : th.color(props.readonly ? "warning" : "accent");
    const omui::Color empty = th.color("track");
    const omui::Color preview = th.color("accent").with_alpha(0.35f * static_cast<float>(hover_.value()));
    for (int i = 0; i < std::max(props.max, 1); ++i) {
        const omui::Rect r{i * (star + gap), (ctx.size.height - star) / 2, star, star};
        const auto path = star_path(r);
        ctx.list.fill_path(path, omui::render::FillRule::nonzero, omui::render::Paint::solid(empty));
        if (preview_ > i && hover_.value() > 0) {
            ctx.list.push_clip({r.x, r.y, r.width * static_cast<float>(std::min(1.0, preview_ - i)), r.height});
            ctx.list.fill_path(path, omui::render::FillRule::nonzero, omui::render::Paint::solid(preview));
            ctx.list.pop_clip();
        }
        if (shown > i) {
            ctx.list.push_clip({r.x, r.y, r.width * static_cast<float>(std::min(1.0, shown - i)), r.height});
            ctx.list.fill_path(path, omui::render::FillRule::nonzero, omui::render::Paint::solid(on));
            ctx.list.pop_clip();
        }
    }
    if (ctx.state.focused && ctx.state.focus_visible)
        omui::parts::focus_ring(ctx, {-3, -3, ctx.size.width + 6, ctx.size.height + 6}, omui::CornerRadii::all(th.corner("small", 4)));
    omui::parts::keep_frames(ctx, {&fill_, &hover_});
}

bool Rating::on_event(omui::EventContext& ctx, const omui::InputEvent& e) {
    const auto* p = std::get_if<omui::PointerEvent>(&e);
    if (!p || props.disabled || props.readonly) return false;
    switch (p->kind) {
        case omui::PointerKind::move:
        case omui::PointerKind::enter:
            if (const double v = at(ctx.local.x); v != preview_) {
                preview_ = v;
                services().invalidate_paint(node());
            }
            return p->kind == omui::PointerKind::move;
        case omui::PointerKind::leave:
            preview_ = -1;
            services().invalidate_paint(node());
            return false;
        case omui::PointerKind::down:
            if (p->button != omui::MouseButton::left && p->type == omui::PointerType::mouse) return false;
            // A click on the rating already given takes it away.
            (void)change(at(ctx.local.x) == props.value ? 0.0 : at(ctx.local.x));
            return true;
        default:
            return false;
    }
}

void Rating::semantics(omui::SemanticsContext&, omui::spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = omui::Value{}.set("value", props.value).set("min", 0).set("max", props.max).set("step", step());
    if (props.readonly) node.states.set(omui::State::readonly, true);
}

omui::ActResult Rating::change(double v) {
    if (props.readonly) return omui::ActResult::fail("the rating is read-only");
    v = snap(v);
    if (v != props.value) {
        write_value(v);
        emit_changed(v);
        fill_.to(v, services().theme().transition("motion.spring.snappy"));
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    return omui::ActResult::done(omui::Value(props.value));
}

omui::ActResult Rating::set_value(omui::ActContext&, const omui::Value& value) {
    return change(value.as_string() == "max" ? static_cast<double>(props.max) : value.as_double());
}
omui::ActResult Rating::increment(omui::ActContext&) { return change(props.value + step()); }
omui::ActResult Rating::decrement(omui::ActContext&) { return change(props.value - step()); }
omui::ActResult Rating::clear(omui::ActContext&) { return change(0); }

}  // namespace shelf
