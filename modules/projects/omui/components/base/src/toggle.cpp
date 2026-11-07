module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

using parts::focus_ring;
using parts::mix;

namespace {

constexpr float track_w = 40, track_h = 22, thumb_inset = 2;
constexpr float box_size = 18;
constexpr float gap = 8;

Color label_color(const Theme& t, bool off) { return off ? t.color("text_disabled") : t.color("text"); }

Size with_label(parts::Label& label, Services& s, Color color, Size control, float available = std::numeric_limits<float>::infinity()) {
    if (label.text().empty()) return control;
    const Size t = label.measure(s, color, std::max(0.0f, available - control.width - gap));
    return {control.width + gap + std::ceil(t.width), std::max(control.height, std::ceil(t.height))};
}

}  // namespace

// ── toggle ──

void Toggle::mounted() {
    thumb_ = props.checked ? 1 : 0;
    hover_ = 0;
    label_.set_style("body");
    label_.set_text(props.label);
}

void Toggle::prop_changed(ToggleProp p) {
    if (p == ToggleProp::label) {
        label_.set_text(props.label);
        if (attached()) services().invalidate_layout(node());
    }
    if (p == ToggleProp::checked && attached()) {
        // A change from outside (a binding) moves the thumb as a click does.
        thumb_ = props.checked ? 1 : 0;
        services().invalidate_paint(node());
    }
}

Size Toggle::measure(MeasureContext& m, std::optional<float> width, std::optional<float>, mlibs::layout::Available aw, mlibs::layout::Available) {
    const float room = width.value_or(aw.is_definite() ? aw.value : std::numeric_limits<float>::infinity());
    return with_label(label_, m.services, label_color(m.services.theme(), props.disabled), {track_w, track_h}, room);
}

float Toggle::baseline(MeasureContext& m, Size content) {
    const Color c = label_color(m.services.theme(), props.disabled);
    if (label_.text().empty()) return content.height;
    const Size t = label_.measure(m.services, c, std::max(0.0f, content.width - track_w - gap));
    return (content.height - t.height) / 2 + label_.baseline(m.services, c);
}

// Its surfaces for an effect: the track and the thumb where it is now.
std::size_t Toggle::effect_shapes(Size, Rect content, std::span<xrender::EffectShape> out) const {
    if (out.size() < 2) return 0;
    const float t = static_cast<float>(thumb_);
    const float y = content.y + (content.height - track_h) / 2;
    const float d = track_h - 2 * thumb_inset;
    const float x = content.x + thumb_inset + t * (track_w - 2 * thumb_inset - d);
    out[0] = {{content.x, y, track_w, track_h}, CornerRadii::all(track_h / 2)};
    out[1] = {{x, y + thumb_inset, d, d}, CornerRadii::all(d / 2)};
    return 2;
}

void Toggle::paint(PaintContext& ctx) const {
    shown_ = true;
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    hover_ = !off && ctx.state.hovered ? 1.0 : 0.0;
    const float t = static_cast<float>(thumb_);
    const float k = std::clamp(t, 0.0f, 1.0f);
    const Rect content = ctx.content;
    const float y = content.y + (content.height - track_h) / 2;
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});

    Color track = mix(th.color("track"), th.color("accent"), k);
    track = mix(track, mix(track, Color::black(), 0.08f), static_cast<float>(hover_));
    ctx.list.fill_round_rect({content.x, y, track_w, track_h}, CornerRadii::all(track_h / 2), xrender::Paint::solid(track));
    const float d = track_h - 2 * thumb_inset;
    const float x = content.x + thumb_inset + t * (track_w - 2 * thumb_inset - d);
    const Rect thumb{x, y + thumb_inset, d, d};
    parts::depth(ctx, "switch_thumb", thumb, CornerRadii::all(d / 2), hover_ > 0, ctx.state.pressed && !off, ShadowToken{{0, 1}, 2, 0, th.color("scrim").with_alpha(0.25f)});
    ctx.list.fill_round_rect(thumb, CornerRadii::all(d / 2), xrender::Paint::solid(th.color("thumb")));

    if (!label_.text().empty()) {
        const Color lc = label_color(th, false);
        const Size ts = label_.measure(ctx.services, lc, std::max(0.0f, content.width - track_w - gap));
        label_.paint(ctx, {content.x + track_w + gap, content.y + (content.height - ts.height) / 2}, lc);
    }
    if (off) ctx.list.pop_layer();
}

void Toggle::paint_over(PaintContext& ctx) const {
    if (!(ctx.state.focused && ctx.state.focus_visible)) return;
    const float y = ctx.content.y + (ctx.content.height - track_h) / 2;
    focus_ring(ctx, {ctx.content.x, y, track_w, track_h}, CornerRadii::all(track_h / 2));
}

void Toggle::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.states.set(State::checked, props.checked);
    node.value = Value(props.checked);
}

ActResult Toggle::toggle(ActContext&) {
    write_checked(!props.checked);
    thumb_ = props.checked ? 1 : 0;
    emit_changed(props.checked);
    services().invalidate_paint(node());
    return ActResult::done(Value(props.checked)).undo_by("toggle");  // toggling again takes it back (S-12)
}

ActResult Toggle::preview(ActContext&, std::string_view) const { return ActResult::done(Value(!props.checked)).undo_by("toggle"); }

// ── checkbox ──

void Checkbox::mounted() {
    mark_ = props.checked || props.indeterminate ? 1 : 0;
    hover_ = 0;
    label_.set_style("body");
    label_.set_text(props.label);
}

void Checkbox::prop_changed(CheckboxProp p) {
    if (p == CheckboxProp::label) {
        label_.set_text(props.label);
        if (attached()) services().invalidate_layout(node());
    }
    if ((p == CheckboxProp::checked || p == CheckboxProp::indeterminate) && attached()) {
        const double on = props.checked || props.indeterminate ? 1 : 0;
        mark_ = on;
        services().invalidate_paint(node());
    }
}

Size Checkbox::measure(MeasureContext& m, std::optional<float> width, std::optional<float>, mlibs::layout::Available aw, mlibs::layout::Available) {
    const float room = width.value_or(aw.is_definite() ? aw.value : std::numeric_limits<float>::infinity());
    return with_label(label_, m.services, label_color(m.services.theme(), props.disabled), {box_size, box_size}, room);
}

float Checkbox::baseline(MeasureContext& m, Size content) {
    const Color c = label_color(m.services.theme(), props.disabled);
    if (label_.text().empty()) return content.height;
    const Size t = label_.measure(m.services, c, std::max(0.0f, content.width - box_size - gap));
    return (content.height - t.height) / 2 + label_.baseline(m.services, c);
}

void Checkbox::paint(PaintContext& ctx) const {
    shown_ = true;
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    hover_ = !off && ctx.state.hovered ? 1.0 : 0.0;
    const float k = static_cast<float>(std::clamp(mark_, 0.0, 1.0));
    const Rect content = ctx.content;
    const Rect b{content.x, content.y + (content.height - box_size) / 2, box_size, box_size};
    const CornerRadii r = CornerRadii::all(th.corner("small", 4));
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});

    const Color empty = mix(th.color("surface"), th.color("surface_variant"), static_cast<float>(hover_));
    ctx.list.fill_round_rect(b, r, xrender::Paint::solid(mix(empty, th.color("accent"), k)));
    if (k < 1) {
        const Color strong = th.color("border_strong");
        const Color border = strong.with_alpha(strong.a * (1 - k));
        ctx.list.stroke_round_rect({b.x + 0.5f, b.y + 0.5f, b.width - 1, b.height - 1}, CornerRadii::all(r.top_left - 0.5f), 1,
                                   xrender::Paint::solid(border));
    }
    if (k > 0) {
        const Color mark = th.color("on_accent");
        if (props.indeterminate) {
            const float w = 10 * k;
            ctx.list.fill_round_rect({b.x + (box_size - w) / 2, b.y + box_size / 2 - 1, w, 2}, CornerRadii::all(1), xrender::Paint::solid(mark));
        } else {
            // The check draws itself in: a dash as long as the part of the path shown.
            xrender::Path check;
            const Point p0{b.x + 4.5f, b.y + 9.5f}, p1{b.x + 7.75f, b.y + 12.75f}, p2{b.x + 13.5f, b.y + 5.5f};
            check.move_to(p0).line_to(p1).line_to(p2);
            // sqrt is correctly rounded everywhere (hypot is not): the same pixels on every platform.
            auto dist = [](Point a, Point b) { return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)); };
            const float length = dist(p0, p1) + dist(p1, p2);
            const std::array<float, 2> dash{length * k, length};
            xrender::StrokeStyle st;
            st.width = 2;
            st.cap = xrender::LineCap::round;
            st.join = xrender::LineJoin::round;
            st.dashes = dash;
            ctx.list.stroke_path(check, st, xrender::Paint::solid(mark));
        }
    }
    if (!label_.text().empty()) {
        const Color lc = label_color(th, false);
        const Size ts = label_.measure(ctx.services, lc, std::max(0.0f, content.width - box_size - gap));
        label_.paint(ctx, {content.x + box_size + gap, content.y + (content.height - ts.height) / 2}, lc);
    }
    if (off) ctx.list.pop_layer();
}

void Checkbox::paint_over(PaintContext& ctx) const {
    if (!(ctx.state.focused && ctx.state.focus_visible)) return;
    const Rect b{ctx.content.x, ctx.content.y + (ctx.content.height - box_size) / 2, box_size, box_size};
    focus_ring(ctx, b, CornerRadii::all(ctx.theme.corner("small", 4)));
}

void Checkbox::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.states.set(State::checked, props.checked && !props.indeterminate);
    node.states.set(State::mixed, props.indeterminate);
    node.value = props.indeterminate ? Value("mixed") : Value(props.checked);
}

ActResult Checkbox::toggle(ActContext&) {
    // Indeterminate goes to checked, as platforms do.
    const bool next = props.indeterminate ? true : !props.checked;
    const bool was_indeterminate = props.indeterminate;
    if (props.indeterminate) props.indeterminate = false;
    write_checked(next);
    mark_ = next ? 1 : 0;
    emit_changed(next);
    services().invalidate_paint(node());
    // Toggling again takes it back — unless it was indeterminate, which no action restores.
    ActResult r = ActResult::done(Value(next));
    if (!was_indeterminate) r.undo_by("toggle");
    return r;
}

ActResult Checkbox::preview(ActContext&, std::string_view) const {
    ActResult r = ActResult::done(Value(props.indeterminate ? true : !props.checked));
    if (!props.indeterminate) r.undo_by("toggle");
    return r;
}

}  // namespace omui
