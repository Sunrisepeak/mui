module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

using parts::focus_ring;
using parts::mix;

namespace {

constexpr float height = 36;
constexpr float padding_x = 16;
constexpr float padding_y = 6;  // a label wrapped onto more lines keeps off the edge
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
    hover_ = 0;
    press_ = 0;
    label_.set_text(caption());
}

void Button::prop_changed(ButtonProp p) {
    if (p == ButtonProp::text || p == ButtonProp::icon) {
        label_.set_text(caption());
        if (attached()) services().invalidate_layout(node());
    }
}

void Button::restyle() { label_.set_style("label"); }

void Button::style(const Theme& theme, mlibs::layout::Style& s) const {
    s.padding = {theme.scaled(padding_y), theme.scaled(padding_x), theme.scaled(padding_y), theme.scaled(padding_x)};
    s.min_height = mlibs::layout::pt(theme.scaled(height));
    s.min_width = mlibs::layout::pt(theme.scaled(height));
}

Size Button::measure(MeasureContext& m, std::optional<float> width, std::optional<float>, mlibs::layout::Available aw, mlibs::layout::Available) {
    const Colors c = colors(m.services.theme(), props.variant);
    // Narrower than its label (a max_width, a narrow column): the label wraps.
    // Wrapped between words only: a word wider than the room stays whole.
    float room = width ? *width : aw.is_definite() ? aw.value : std::numeric_limits<float>::infinity();
    if (std::isfinite(room)) room = std::max(room, std::ceil(label_.min_content(m.services, c.text)));
    Size s = label_.measure(m.services, c.text, room);
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
    hover_ = !off && (ctx.state.hovered || ctx.state.pressed) ? 1.0 : 0.0;
    press_ = !off && ctx.state.pressed ? 1.0 : 0.0;
    const float h = static_cast<float>(hover_);
    const float p = static_cast<float>(std::clamp(press_, -0.5, 1.5));

    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    corner_ = ctx.theme.corner("medium", 8);
    const CornerRadii radii = CornerRadii::all(corner_).fitted(ctx.size);
    const float s = 1 - press_scale * p;
    const bool scaled = s != 1;
    if (scaled) {
        const Point center = box.center();
        ctx.list.push_transform(Affine::translate(center.x, center.y) * Affine::scale(s, s) * Affine::translate(-center.x, -center.y));
    }
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, box);

    Color fill = mix(c.base, c.hover, h);
    fill = mix(fill, c.pressed, std::clamp(p, 0.0f, 1.0f));
    if (fill.a > 0) ctx.list.fill_round_rect(box, radii, xrender::Paint::solid(fill));
    if (c.border) {
        CornerRadii inner = radii;
        for (float* r : {&inner.top_left, &inner.top_right, &inner.bottom_right, &inner.bottom_left}) *r = std::max(0.0f, *r - 0.5f);
        ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, inner, 1, xrender::Paint::solid(*c.border));
    }
    const Color text = off ? ctx.theme.color("text_disabled", c.text) : c.text;
    const Size t = label_.measure(ctx.services, text, std::max(ctx.content.width + 0.5f, std::ceil(label_.min_content(ctx.services, text)) + 0.5f));
    label_.paint(ctx, {ctx.content.x + (ctx.content.width - t.width) / 2, ctx.content.y + (ctx.content.height - t.height) / 2}, text);

    if (off) ctx.list.pop_layer();
    if (scaled) ctx.list.pop_transform();
}

void Button::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible)
        focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.theme.corner("medium", 8)).fitted(ctx.size));
}

void Button::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.text;  // the label is the name (S-6)
    if (props.disabled) node.states.set(State::disabled);
}

std::size_t Button::effect_shapes(Size size, Rect, std::span<xrender::EffectShape> out) const {
    if (out.empty()) return 0;
    out[0] = {{0, 0, size.width, size.height}, CornerRadii::all(corner_).fitted(size)};
    return 1;
}

ActResult Button::activate(ActContext&) {
    emit_activated();
    return ActResult::done();
}

}  // namespace omui
