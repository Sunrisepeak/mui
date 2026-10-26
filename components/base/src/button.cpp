module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

using parts::focus_ring;
using parts::keep_frames;
using parts::mix;

namespace {

constexpr float height = 36;
constexpr float padding_x = 16;
constexpr float press_scale = 0.03f;  // a pressed button is this much smaller

}  // namespace

Button::Colors Button::colors(const Theme& t, ButtonVariant v) {
    switch (v) {
        case ButtonVariant::primary:
            return {t.color("accent"), t.color("accent_hover"), t.color("accent_pressed"), t.color("on_accent"), std::nullopt};
        case ButtonVariant::danger: {
            const Color d = t.color("danger");
            return {d, mix(d, Color::black(), 0.12f), mix(d, Color::black(), 0.24f), t.color("on_danger"), std::nullopt};
        }
        case ButtonVariant::ghost:
            return {Color::transparent(), t.color("surface_variant"), t.color("surface_pressed"), t.color("accent"), std::nullopt};
        case ButtonVariant::secondary:
            break;
    }
    return {t.color("surface"), t.color("surface_variant"), t.color("surface_pressed"), t.color("text"), t.color("border")};
}

std::string Button::caption() const { return props.icon.empty() ? props.text : std::format("{}  {}", props.icon, props.text); }

void Button::mounted() {
    hover_.attach(services(), 0);
    press_.attach(services(), 0);
    label_.set_text(caption());
}

void Button::prop_changed(ButtonProp p) {
    if (p == ButtonProp::text || p == ButtonProp::icon) {
        label_.set_text(caption());
        if (attached()) services().invalidate_layout(node());
    }
}

void Button::restyle() { label_.set_style("label"); }

void Button::style(const Theme&, layout::Style& s) const {
    s.padding = {0, padding_x, 0, padding_x};
    s.min_height = layout::pt(height);
    s.min_width = layout::pt(height);
}

Size Button::measure(MeasureContext& m, std::optional<float> width, std::optional<float>, layout::Available, layout::Available) {
    const Colors c = colors(m.services.theme(), props.variant);
    Size s = label_.measure(m.services, c.text);
    return {width.value_or(std::ceil(s.width)), std::ceil(s.height)};
}

float Button::baseline(MeasureContext& m, Size content) {
    const Colors c = colors(m.services.theme(), props.variant);
    const Size s = label_.measure(m.services, c.text);
    return (content.height - s.height) / 2 + label_.baseline(m.services, c.text);
}

void Button::paint(PaintContext& ctx) const {
    const Colors c = colors(ctx.theme, props.variant);
    const bool off = props.disabled || ctx.state.disabled;
    // The hover and press feedback move with the contract's motion tokens.
    hover_.to(!off && (ctx.state.hovered || ctx.state.pressed) ? 1.0 : 0.0, ctx.theme.transition("motion.fast"));
    press_.to(!off && ctx.state.pressed ? 1.0 : 0.0, ctx.theme.transition("motion.spring.snappy"));
    const float h = static_cast<float>(hover_.value());
    const float p = static_cast<float>(std::clamp(press_.value(), -0.5, 1.5));

    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii radii = CornerRadii::all(ctx.theme.corner("medium", 8)).fitted(ctx.size);
    const float s = 1 - press_scale * p;
    const bool scaled = s != 1;
    if (scaled) {
        const Point center = box.center();
        ctx.list.push_transform(Affine::translate(center.x, center.y) * Affine::scale(s, s) * Affine::translate(-center.x, -center.y));
    }
    if (off) ctx.list.push_layer(0.5f, render::BlendMode::normal, {}, {}, box);

    Color fill = mix(c.base, c.hover, h);
    fill = mix(fill, c.pressed, std::clamp(p, 0.0f, 1.0f));
    if (fill.a > 0) ctx.list.fill_round_rect(box, radii, render::Paint::solid(fill));
    if (c.border) {
        CornerRadii inner = radii;
        for (float* r : {&inner.top_left, &inner.top_right, &inner.bottom_right, &inner.bottom_left}) *r = std::max(0.0f, *r - 0.5f);
        ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, inner, 1, render::Paint::solid(*c.border));
    }
    const Color text = off ? ctx.theme.color("text_disabled", c.text) : c.text;
    const Size t = label_.measure(ctx.services, text);
    label_.paint(ctx, {ctx.content.x + (ctx.content.width - t.width) / 2, ctx.content.y + (ctx.content.height - t.height) / 2}, text);

    if (off) ctx.list.pop_layer();
    if (scaled) ctx.list.pop_transform();
    keep_frames(ctx, {&hover_, &press_});
}

void Button::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible)
        focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.theme.corner("medium", 8)).fitted(ctx.size));
}

void Button::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.text;  // the label is the name (S-6)
    if (props.disabled) node.states.set(State::disabled);
}

ActResult Button::activate(ActContext&) {
    emit_activated();
    return ActResult::done();
}

}  // namespace omui
