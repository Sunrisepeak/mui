// openmui.ui.base:toggle and :checkbox share the shape of a two-state control
// with a label: a box or track at the start, the label after it, the whole
// clickable, Space (and Enter for a switch) toggling it.
export module openmui.ui.base:toggle;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :toggle_contract;
import :checkbox_contract;

export namespace omui {

// An on/off switch (role `switch`): the thumb springs across the track.
class Toggle : public ToggleContract<Toggle> {
public:
    void mounted();
    void prop_changed(ToggleProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    float baseline(MeasureContext& m, Size content);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    std::size_t effect_shapes(Size size, Rect content, std::span<render::EffectShape> out) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult toggle(ActContext& ctx);
    ActResult activate(ActContext& ctx) { return toggle(ctx); }

private:
    mutable parts::Label  label_;
    mutable parts::Motion thumb_, hover_;
    mutable bool   shown_ = false;
};

// A checkable option, possibly indeterminate (role `checkbox`): the mark draws itself in.
class Checkbox : public CheckboxContract<Checkbox> {
public:
    void mounted();
    void prop_changed(CheckboxProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    float baseline(MeasureContext& m, Size content);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult toggle(ActContext& ctx);
    ActResult activate(ActContext& ctx) { return toggle(ctx); }

private:
    mutable parts::Label  label_;
    mutable parts::Motion mark_, hover_;
    mutable bool   shown_ = false;
};

}  // namespace omui
