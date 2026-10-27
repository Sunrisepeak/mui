module openmui.ui.liquid_glass;

import std;
import openmui.core;
import openmui.view;
import openmui.render;

namespace omui {

namespace {

// The glass's material as the built-in effect's parameters, in its order:
// thickness, ior, blur, dispersion, tint rgba, light xy, smoothness,
// adaptive, rim.
template <class Props>
std::vector<float> material(const Props& p, double smoothness, const Theme& theme) {
    const Color tint = theme.color(p.tint, Color{1, 1, 1, 0.1f});
    return {static_cast<float>(p.thickness), static_cast<float>(p.ior),  static_cast<float>(p.blur), static_cast<float>(p.dispersion),
            tint.r,                          tint.g,                     tint.b,                     tint.a,
            static_cast<float>(p.light_x),   static_cast<float>(p.light_y), static_cast<float>(smoothness), p.adaptive ? 1.0f : 0.0f,
            static_cast<float>(p.rim)};
}

std::optional<render::EffectRef> glass_effect() { return render::effects().find("liquid_glass"); }

CornerRadii fitted(double radius, Size size) {
    return CornerRadii::all(std::max(0.0f, std::min(static_cast<float>(radius), std::min(size.width, size.height) / 2)));
}

// An opaque panel where the glass would be (reduced transparency).
void panel(PaintContext& ctx, double radius) {
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = fitted(radius, ctx.size);
    ctx.list.fill_round_rect(box, r, render::Paint::solid(ctx.theme.color("surface", Color::white())));
    ctx.list.stroke_round_rect({0.5f, 0.5f, ctx.size.width - 1, ctx.size.height - 1}, r, 1, render::Paint::solid(ctx.theme.color("border")));
}

}  // namespace

// ── liquid_glass ─────────────────────────────────────────────────────────

void LiquidGlass::style(const Theme&, layout::Style& s) const {
    s.direction = layout::Direction::column;
    s.padding = Insets::all(static_cast<float>(props.padding));
}

void LiquidGlass::prop_changed(LiquidGlassProp p) {
    if (!attached()) return;
    if (p == LiquidGlassProp::padding) services().restyle(node());
    services().invalidate_paint(node());
    if (p == LiquidGlassProp::radius) {
        // A group's shapes include this member's radius.
        if (const NodeId parent = services().parent(node()); parent && grouped()) services().invalidate_paint(parent);
    }
}

bool LiquidGlass::grouped() const {
    if (!attached()) return false;
    const NodeId parent = services().parent(node());
    // Only a glass_group has `smoothness`.
    return parent && !services().get(parent, "smoothness").is_null();
}

void LiquidGlass::paint(PaintContext& ctx) const {
    if (services().reduce_transparency()) {
        panel(ctx, props.radius);
        return;
    }
    if (grouped()) return;
    const auto ref = glass_effect();
    if (!ref) return;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const std::array<render::EffectShape, 1> shape{{{box, fitted(props.radius, ctx.size)}}};
    const std::vector<float> params = material(props, 0, ctx.theme);
    ctx.list.push_layer(1, render::BlendMode::normal, *ref, params, box, shape);
}

void LiquidGlass::paint_over(PaintContext& ctx) const {
    if (!services().reduce_transparency() && !grouped() && glass_effect()) ctx.list.pop_layer();
}

void LiquidGlass::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (!props.label.empty()) node.name = props.label;
}

// ── glass_group ──────────────────────────────────────────────────────────

void GlassGroup::style(const Theme&, layout::Style& s) const {
    s.direction = props.direction == GlassGroupDirection::column ? layout::Direction::column : layout::Direction::row;
    s.row_gap = s.column_gap = static_cast<float>(props.gap);
}

void GlassGroup::prop_changed(GlassGroupProp p) {
    if (!attached()) return;
    if (p == GlassGroupProp::direction || p == GlassGroupProp::gap) services().restyle(node());
    services().invalidate_paint(node());
}

std::vector<render::EffectShape> GlassGroup::shapes() const {
    std::vector<render::EffectShape> out;
    if (!attached()) return out;
    const Rect me = services().bounds(node());
    for (const NodeId c : services().children(node())) {
        const Value radius = services().get(c, "radius");
        if (radius.is_null() || services().get(c, "ior").is_null()) continue;  // not a liquid_glass
        const Rect b = services().bounds(c);
        const Rect local{b.x - me.x, b.y - me.y, b.width, b.height};
        out.push_back({local, fitted(radius.as_double(24), {b.width, b.height})});
        if (out.size() == 8) break;
    }
    return out;
}

void GlassGroup::paint(PaintContext& ctx) const {
    if (services().reduce_transparency()) return;  // each member is its own panel
    const auto ref = glass_effect();
    const auto members = shapes();
    if (!ref || members.empty()) return;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const std::vector<float> params = material(props, props.smoothness, ctx.theme);
    ctx.list.push_layer(1, render::BlendMode::normal, *ref, params, box, members);
}

void GlassGroup::paint_over(PaintContext& ctx) const {
    if (!services().reduce_transparency() && glass_effect() && !shapes().empty()) ctx.list.pop_layer();
}

void GlassGroup::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (!props.label.empty()) node.name = props.label;
}

void register_liquid_glass_components() {
    register_type(component_type<LiquidGlass>());
    register_type(component_type<GlassGroup>());
}

}  // namespace omui
