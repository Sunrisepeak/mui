// openmui.ui.liquid_glass:glass — the components.
//
// A liquid_glass draws its glass as an effect layer around itself and its
// children: the layer's shape is its rounded box, the effect reads what the
// frame holds under it. In a glass_group, the group draws one layer whose
// shapes are its glass members' boxes — merged smoothly — and the members
// draw none of their own. With reduced transparency every piece of glass is
// an opaque panel in the theme's surface colour, edged in its border colour.
// None of this touches the semantic tree: both are ordinary groups.
export module openmui.ui.liquid_glass:glass;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import :liquid_glass_contract;
import :glass_group_contract;

export namespace omui {

class LiquidGlass : public LiquidGlassContract<LiquidGlass> {
public:
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    void prop_changed(LiquidGlassProp p);

    // Its glass is its group's (the parent is a glass_group).
    [[nodiscard]] bool grouped() const;
};

class GlassGroup : public GlassGroupContract<GlassGroup> {
public:
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    void prop_changed(GlassGroupProp p);

    // The members' shapes, in the group's coordinates (at most 8: the
    // effect's): each liquid_glass child's laid-out box and radius.
    [[nodiscard]] std::vector<render::EffectShape> shapes() const;
};

// Registers liquid_glass and glass_group by name, for view documents and
// immediate mode.
void register_liquid_glass_components();

}  // namespace omui
