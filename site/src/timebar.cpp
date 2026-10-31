module openmui.site;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.base;

namespace omui::site {

namespace {

constexpr float bar_height = 36;
constexpr float inset = 6;  // the strip's ends, so the playhead's knob fits at either

}  // namespace

double Timebar::last() const { return props.end; }

double Timebar::first() const { return props.live ? std::max(0.0, last() - props.window) : props.start; }

float Timebar::x_of(double t, float width) const noexcept {
    const double a = first(), span = std::max(1e-6, last() - a);
    return inset + static_cast<float>(std::clamp((t - a) / span, 0.0, 1.0)) * std::max(0.0f, width - 2 * inset);
}

void Timebar::prop_changed(TimebarProp) {
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size Timebar::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, layout::Available aw, layout::Available) {
    // A small basis: it grows into the row's room, and gives it back on a phone.
    (void)aw;
    return {width.value_or(96.0f), height.value_or(bar_height)};
}

void Timebar::change(double t) {
    t = std::clamp(t, first(), std::max(first(), last()));
    if (props.live || t != props.time) {  // live, any time is a change: the page leaves the present
        write_time(t);
        emit_scrubbed(t);
    }
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

void Timebar::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    size_ = ctx.size;
    auto& out = ctx.list;
    const float w = ctx.size.width, h = ctx.size.height;
    const Rect strip{inset, 8, std::max(0.0f, w - 2 * inset), h - 16};
    out.fill_round_rect(strip, CornerRadii::all(4), render::Paint::solid(th.color("track")));
    // What has been played up to the playhead, and every second along it (every fifth taller).
    const double a = first(), b = last();
    const float head = x_of(props.live ? b : props.time, w);
    out.fill_round_rect({strip.x, strip.y, head - strip.x, strip.height}, CornerRadii::all(4), render::Paint::solid(th.color("accent").with_alpha(0.22f)));
    const double span = b - a;
    if (span > 0 && span < 600)
        for (double s = std::ceil(a); s <= b; s += 1) {
            const float x = x_of(s, w);
            const bool fifth = std::fmod(s, 5.0) == 0;
            out.fill_rect({x, strip.y + strip.height - (fifth ? 7.0f : 4.0f), 1, fifth ? 7.0f : 4.0f}, th.color("text_muted").with_alpha(0.5f));
        }
    // What happened: a line where it did, its caption beside it where there is room.
    float free_from = 0;
    std::size_t used = 0;
    if (const Value::List* marks = props.marks.list())
        for (const Value& m : *marks) {
            const double at = m.get("at") ? m.get("at")->as_double(0) : 0;
            if (at < a || at > b) continue;
            const float x = x_of(at, w);
            out.fill_rect({x - 0.5f, strip.y - 3, 1.5f, strip.height + 6}, th.color("accent"));
            if (used >= labels_.size()) labels_.emplace_back(), labels_.back().set_style("caption");
            parts::Label& l = labels_[used];
            l.set_text(std::string(m.get("label") ? m.get("label")->as_string() : std::string_view{}));
            const Size s = l.measure(services(), th.color("text"));
            if (x + 4 < free_from || x + 4 + s.width > strip.x + strip.width) continue;
            l.paint(ctx, {x + 4, strip.y + (strip.height - s.height) / 2}, th.color("text"));
            free_from = x + 4 + s.width + 8;
            ++used;
        }
    // The playhead.
    out.fill_rect({head - 1, 2, 2, h - 4}, th.color("text"));
    out.fill_round_rect({head - 5, 1, 10, 10}, CornerRadii::all(5), render::Paint::solid(th.color("text")));
}

bool Timebar::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    const float w = size_.width > 0 ? size_.width : 320.0f;
    auto time_at = [&](float x) {
        const double a = first();
        return a + (last() - a) * std::clamp((x - inset) / std::max(1.0f, w - 2 * inset), 0.0f, 1.0f);
    };
    switch (p->kind) {
        case PointerKind::down:
            if (p->button != MouseButton::left && p->type == PointerType::mouse) return false;
            dragging_ = true;
            services().capture_pointer(node());
            change(time_at(ctx.local.x));
            return true;
        case PointerKind::move:
            if (!dragging_) return false;
            change(time_at(ctx.local.x));
            return true;
        case PointerKind::up:
        case PointerKind::cancel:
            if (!dragging_) return false;
            dragging_ = false;
            services().release_pointer();
            return true;
        default:
            return false;
    }
}

void Timebar::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("value", props.live ? last() : props.time).set("min", first()).set("max", last()).set("step", step);
}

// Each move is taken back by moving the playhead where it was (S-12).
ActResult Timebar::increment(ActContext&) {
    const double old = props.live ? last() : props.time;
    change(old + step);
    return ActResult::done(Value(props.time)).undo_by("set_value", Value{}.set("value", old));
}

ActResult Timebar::decrement(ActContext&) {
    const double old = props.live ? last() : props.time;
    change(old - step);
    return ActResult::done(Value(props.time)).undo_by("set_value", Value{}.set("value", old));
}

ActResult Timebar::set_value(ActContext&, const Value& value) {
    const double old = props.live ? last() : props.time;
    if (value.is_number()) change(value.as_double());
    else if (value.as_string() == "min") change(first());
    else if (value.as_string() == "max") change(last());
    else return ActResult::fail(std::format("set_value takes a number of seconds (or min, max), not {}", value.json()));
    return ActResult::done(Value(props.time)).undo_by("set_value", Value{}.set("value", old));
}

}  // namespace omui::site
