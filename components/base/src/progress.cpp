module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

using parts::keep_frames;
using parts::mix;

namespace {

constexpr float bar_h = 6, default_w = 240;
constexpr float circle = 18, gap = 8;
constexpr double sweep = 1.4;  // seconds an indeterminate bar takes to cross

}  // namespace

// ── progress ──

void Progress::mounted() { fill_.attach(services(), std::clamp(props.value, 0.0, 1.0)); }

void Progress::prop_changed(ProgressProp p) {
    if (p != ProgressProp::value || !attached()) return;
    if (props.value >= 0) fill_.to(std::clamp(props.value, 0.0, 1.0), services().theme().transition("motion.normal"));
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size Progress::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, layout::Available aw, layout::Available) {
    return {width.value_or(aw.is_definite() ? std::min(aw.value, default_w) : default_w), height.value_or(bar_h)};
}

void Progress::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect track{0, (ctx.size.height - bar_h) / 2, ctx.size.width, bar_h};
    const CornerRadii round = CornerRadii::all(bar_h / 2);
    ctx.list.fill_round_rect(track, round, render::Paint::solid(th.color("track")));
    if (props.value < 0) {
        // Indeterminate: a segment sweeps across, on the runtime's clock.
        const double phase = std::fmod(ctx.services.now().count() / sweep, 1.0);
        const float w = track.width * 0.3f;
        const float x = static_cast<float>(phase) * (track.width + w) - w;
        ctx.list.push_clip(track, round);
        ctx.list.fill_round_rect({x, track.y, w, bar_h}, round, render::Paint::solid(th.color("accent")));
        ctx.list.pop_clip();
        ctx.services.invalidate_paint(ctx.node);
        ctx.services.request_frame();
        return;
    }
    const float f = static_cast<float>(std::clamp(fill_.value(), 0.0, 1.0));
    if (f > 0) ctx.list.fill_round_rect({0, track.y, track.width * f, bar_h}, round, render::Paint::solid(th.color("accent")));
    keep_frames(ctx, {&fill_});
}

void Progress::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    if (props.value < 0) node.states.set(State::busy);
    else node.value = Value(props.value);
}

// ── radio ──

void Radio::mounted() {
    dot_.attach(services(), chosen() ? 1 : 0);
    hover_.attach(services(), 0);
    label_.set_style("body");
    label_.set_text(props.label);
}

void Radio::prop_changed(RadioProp p) {
    if (p == RadioProp::label) {
        label_.set_text(props.label);
        if (attached()) services().invalidate_layout(node());
    }
    if ((p == RadioProp::group || p == RadioProp::value) && attached()) {
        if (shown_) dot_.to(chosen() ? 1 : 0, services().theme().transition("motion.fast"));
        else dot_.jump(chosen() ? 1 : 0);
        services().invalidate_paint(node());
    }
}

Size Radio::measure(MeasureContext& m, std::optional<float>, std::optional<float>, layout::Available, layout::Available) {
    if (label_.text().empty()) return {circle, circle};
    const Size t = label_.measure(m.services, m.services.theme().color("text"));
    return {circle + gap + std::ceil(t.width), std::max(circle, std::ceil(t.height))};
}

float Radio::baseline(MeasureContext& m, Size content) {
    if (label_.text().empty()) return content.height;
    const Color c = m.services.theme().color("text");
    const Size t = label_.measure(m.services, c);
    return (content.height - t.height) / 2 + label_.baseline(m.services, c);
}

void Radio::paint(PaintContext& ctx) const {
    shown_ = true;
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    hover_.to(!off && ctx.state.hovered ? 1.0 : 0.0, th.transition("motion.fast"));
    const float k = static_cast<float>(std::clamp(dot_.value(), 0.0, 1.0));
    const Rect content = ctx.content;
    const Rect c{content.x, content.y + (content.height - circle) / 2, circle, circle};
    if (off) ctx.list.push_layer(0.5f, render::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});
    ctx.list.fill_round_rect(c, CornerRadii::all(circle / 2),
                             render::Paint::solid(mix(th.color("surface"), th.color("surface_variant"), static_cast<float>(hover_.value()))));
    const Color ring = mix(th.color("border_strong"), th.color("accent"), k);
    const float rw = 1.5f + 0.5f * k;
    ctx.list.stroke_round_rect({c.x + rw / 2, c.y + rw / 2, circle - rw, circle - rw}, CornerRadii::all(circle / 2 - rw / 2), rw,
                               render::Paint::solid(ring));
    if (k > 0) {
        const float r = 4.5f * k;
        const Point m = c.center();
        ctx.list.fill_round_rect({m.x - r, m.y - r, 2 * r, 2 * r}, CornerRadii::all(r), render::Paint::solid(th.color("accent")));
    }
    if (!label_.text().empty()) {
        const Color lc = th.color("text");
        const Size ts = label_.measure(ctx.services, lc);
        label_.paint(ctx, {content.x + circle + gap, content.y + (content.height - ts.height) / 2}, lc);
    }
    if (off) ctx.list.pop_layer();
    keep_frames(ctx, {&dot_, &hover_});
}

void Radio::paint_over(PaintContext& ctx) const {
    if (!(ctx.state.focused && ctx.state.focus_visible)) return;
    const Rect c{ctx.content.x, ctx.content.y + (ctx.content.height - circle) / 2, circle, circle};
    parts::focus_ring(ctx, c, CornerRadii::all(circle / 2));
}

void Radio::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.states.set(State::checked, chosen());
    node.value = Value(props.value);
}

ActResult Radio::select(ActContext&) {
    if (!chosen()) {
        write_group(props.value);  // the group's signal: every radio of it follows
        emit_selected(props.value);
    }
    dot_.to(1, services().theme().transition("motion.fast"));
    services().invalidate_paint(node());
    return ActResult::done(Value(props.value));
}

}  // namespace omui
