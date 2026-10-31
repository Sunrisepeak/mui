// openmui.ui.base:button — performs an action when activated (contract
// `button`): four variants, hover and press feedback (a fast colour change,
// a snappy spring), keyboard focus ring, the verb it invokes classifying its
// effect (S-1).
export module openmui.ui.base:button;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :button_contract;

export namespace omui {

class Button : public ButtonContract<Button> {
public:
    void mounted();
    void prop_changed(ButtonProp p);
    void restyle();

    void style(const Theme& theme, layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    float baseline(MeasureContext& m, Size content);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    // Its surface for an effect: the rounded box it paints (a neon tube follows its corners).
    std::size_t effect_shapes(Size size, Rect content, std::span<render::EffectShape> out) const;

    ActResult activate(ActContext& ctx);

    struct Colors {
        Color base, hover, pressed, text;
        std::optional<Color> border;
    };
    [[nodiscard]] static Colors colors(const Theme& theme, ButtonVariant variant);

private:
    [[nodiscard]] std::string caption() const;

    mutable parts::Label  label_;
    mutable parts::Motion hover_, press_;
    mutable float         corner_ = 8;  // the corners as last painted (the theme's medium)
};

}  // namespace omui
