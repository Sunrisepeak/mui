// openmui.ui.base:number_input — a number one types or steps (contract
// `number_input`): the digits typed while it has focus (Enter or leaving
// keeps them, within min and max, on the step; Escape drops them), the −
// and + buttons and the arrow keys step it. Every change is an action of
// the history (S-12), taken back to the value it replaced exactly.
export module omui.ui.base:number_input;

import std;
import omui.core;
import omui.view;
import :parts;
import :number_input_contract;

export namespace omui {

class NumberInput : public NumberInputContract<NumberInput> {
public:
    void mounted();
    void prop_changed(NumberInputProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    float baseline(MeasureContext& m, Size content);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    std::size_t effect_shapes(Size size, Rect content, std::span<xrender::EffectShape> out) const;

    ActResult increment(ActContext& ctx);
    ActResult decrement(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);

    // The value as shown: its digits after the point, its unit.
    [[nodiscard]] std::string shown() const;
    // A value within min and max, on the step (counted from min when it is finite, else from 0).
    [[nodiscard]] double kept(double v) const noexcept;

private:
    ActResult change(double v, bool exact = false);
    void commit_draft();
    [[nodiscard]] Rect minus_box(Size size) const;
    [[nodiscard]] Rect plus_box(Size size) const;

    mutable parts::Label text_;
    std::optional<std::string> draft_;  // what is being typed
    int  hover_ = 0;                    // -1 the minus, 1 the plus
    bool focused_ = false;
};

}  // namespace omui
