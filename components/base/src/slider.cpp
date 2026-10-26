module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

using parts::focus_ring;
using parts::keep_frames;
using parts::mix;

namespace {

constexpr float track_h = 4;
constexpr float thumb_d = 18;
constexpr float default_w = 200, height = 24;

}  // namespace

double Slider::snap(double v) const noexcept {
    const double lo = std::min(props.min, props.max), hi = std::max(props.min, props.max);
    v = std::clamp(v, lo, hi);
    if (props.step > 0) {
        v = lo + std::round((v - lo) / props.step) * props.step;
        v = std::clamp(v, lo, hi);
    }
    return v;
}

double Slider::fraction(double v) const noexcept {
    const double span = props.max - props.min;
    return span == 0 ? 0 : std::clamp((v - props.min) / span, 0.0, 1.0);
}

Rect Slider::track(Size size) const noexcept {
    return {thumb_d / 2, (size.height - track_h) / 2, std::max(0.0f, size.width - thumb_d), track_h};
}

void Slider::mounted() {
    thumb_.attach(services(), fraction(props.value));
    hover_.attach(services(), 0);
    halo_.attach(services(), 0);
}

void Slider::prop_changed(SliderProp p) {
    if (!attached()) return;
    if (p == SliderProp::value || p == SliderProp::min || p == SliderProp::max) {
        // From outside: glide, unless the user is dragging it.
        if (!dragging_) thumb_.to(fraction(props.value), services().theme().transition("motion.fast"));
        services().invalidate_paint(node());
    }
}

Size Slider::measure(MeasureContext&, std::optional<float> width, std::optional<float> h, layout::Available aw, layout::Available) {
    float w = width.value_or(aw.is_definite() ? std::min(aw.value, default_w) : default_w);
    return {w, h.value_or(height)};
}

bool Slider::change(double v, bool animate, bool commit) {
    v = snap(v);
    const bool moved = v != props.value;
    if (moved) {
        write_value(v);
        emit_changed(v);
    }
    if (animate) thumb_.to(fraction(v), services().theme().transition("motion.fast"));
    else thumb_.jump(fraction(v));
    if (commit && moved) emit_committed(v);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return moved;
}

void Slider::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    size_ = ctx.size;
    const bool off = props.disabled || ctx.state.disabled;
    hover_.to(!off && (ctx.state.hovered || dragging_) ? 1.0 : 0.0, th.transition("motion.fast"));
    halo_.to(!off && (dragging_ || (ctx.state.focused && ctx.state.focus_visible)) ? 1.0 : 0.0, th.transition("motion.fast"));
    const Rect t = track(ctx.size);
    const float f = static_cast<float>(std::clamp(thumb_.value(), 0.0, 1.0));
    if (off) ctx.list.push_layer(0.5f, render::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});

    const CornerRadii round = CornerRadii::all(track_h / 2);
    ctx.list.fill_round_rect(t, round, render::Paint::solid(th.color("track")));
    ctx.list.fill_round_rect({t.x, t.y, t.width * f, t.height}, round, render::Paint::solid(th.color("accent")));
    const Point c{t.x + t.width * f, ctx.size.height / 2};
    const float halo = static_cast<float>(halo_.value()) * 0.5f + static_cast<float>(hover_.value()) * 0.25f;
    if (halo > 0) {
        const float r = thumb_d / 2 + 6;
        ctx.list.fill_round_rect({c.x - r, c.y - r, 2 * r, 2 * r}, CornerRadii::all(r),
                                 render::Paint::solid(th.color("accent").with_alpha(0.18f * std::min(1.0f, halo))));
    }
    const Rect thumb{c.x - thumb_d / 2, c.y - thumb_d / 2, thumb_d, thumb_d};
    ctx.list.box_shadow(thumb, CornerRadii::all(thumb_d / 2), {0, 1}, 3, 0, th.color("scrim").with_alpha(0.3f));
    ctx.list.fill_round_rect(thumb, CornerRadii::all(thumb_d / 2), render::Paint::solid(th.color("thumb")));
    ctx.list.stroke_round_rect({thumb.x + 1, thumb.y + 1, thumb_d - 2, thumb_d - 2}, CornerRadii::all(thumb_d / 2 - 1), 2,
                               render::Paint::solid(th.color("accent")));
    if (off) ctx.list.pop_layer();
    keep_frames(ctx, {&thumb_, &hover_, &halo_});
}

void Slider::paint_over(PaintContext&) const {}  // the halo shows keyboard focus

bool Slider::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || props.disabled) return false;
    const Rect t = track(size_.width > 0 ? size_ : Size{default_w, height});
    auto value_at = [&](float x) { return props.min + (props.max - props.min) * std::clamp((x - t.x) / std::max(1.0f, t.width), 0.0f, 1.0f); };
    switch (p->kind) {
        case PointerKind::down:
            if (p->button != MouseButton::left && p->type == PointerType::mouse) return false;
            dragging_ = true;
            services().capture_pointer(node());
            change(value_at(ctx.local.x), false, false);
            return true;
        case PointerKind::move:
            if (!dragging_) return false;
            change(value_at(ctx.local.x), false, false);
            return true;
        case PointerKind::up:
        case PointerKind::cancel:
            if (!dragging_) return false;
            dragging_ = false;
            services().release_pointer();
            emit_committed(props.value);
            services().invalidate_paint(node());
            return true;  // handled: a drag is not an activation
        default:
            return false;
    }
}

void Slider::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("value", props.value).set("min", props.min).set("max", props.max).set("step", props.step);
}

ActResult Slider::increment(ActContext&) {
    change(props.value + (props.step > 0 ? props.step : (props.max - props.min) / 100), true, true);
    return ActResult::done(Value(props.value));
}

ActResult Slider::decrement(ActContext&) {
    change(props.value - (props.step > 0 ? props.step : (props.max - props.min) / 100), true, true);
    return ActResult::done(Value(props.value));
}

ActResult Slider::set_value(ActContext&, const Value& value) {
    double v = props.value;
    if (value.is_number()) v = value.as_double();
    else if (value.as_string() == "min") v = props.min;
    else if (value.as_string() == "max") v = props.max;
    else return ActResult::fail(std::format("set_value takes a number (or min, max), not {}", value.json()));
    change(v, true, true);
    return ActResult::done(Value(props.value));
}

}  // namespace omui
