// openmui.ui.base:slider — a number in a range, by dragging the thumb,
// clicking the track, keys (arrows step, Home and End jump) or actions;
// `changed` while it moves, `committed` when a drag ends.
export module openmui.ui.base:slider;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :slider_contract;

export namespace omui {

class Slider : public SliderContract<Slider> {
public:
    void mounted();
    void prop_changed(SliderProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult increment(ActContext& ctx);
    ActResult decrement(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);

    // The value a raw number becomes: in range, on a step.
    [[nodiscard]] double snap(double v) const noexcept;

private:
    // Sets the value; `animate` glides the thumb (keys), else it follows (drags).
    bool change(double v, bool animate, bool commit);
    [[nodiscard]] double fraction(double v) const noexcept;
    [[nodiscard]] Rect track(Size size) const noexcept;

    mutable parts::Motion thumb_, hover_, halo_;
    mutable Size   size_{};  // the last painted size: pointer positions map onto it
    bool           dragging_ = false;
};

}  // namespace omui
