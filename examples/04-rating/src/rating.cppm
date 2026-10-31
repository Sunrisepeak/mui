export module shelf:rating;

import std;
import openmui.core;
import openmui.view;
import openmui.ui.base;
import :rating_contract;

export namespace shelf {

class Rating : public RatingContract<Rating> {
public:
    void mounted();
    void prop_changed(RatingProp p);
    omui::Size measure(omui::MeasureContext& m, std::optional<float> width, std::optional<float> height, omui::layout::Available aw,
                       omui::layout::Available ah);
    void paint(omui::PaintContext& ctx) const;
    bool on_event(omui::EventContext& ctx, const omui::InputEvent& e);
    void semantics(omui::SemanticsContext& ctx, omui::spec::SemanticNode& node) const;

    omui::ActResult set_value(omui::ActContext& ctx, const omui::Value& value);
    omui::ActResult increment(omui::ActContext& ctx);
    omui::ActResult decrement(omui::ActContext& ctx);
    omui::ActResult clear(omui::ActContext& ctx);

    // A value as the rating keeps it: from 0 to max, in whole or half stars.
    [[nodiscard]] double snap(double v) const noexcept;
    [[nodiscard]] double step() const noexcept { return props.half ? 0.5 : 1.0; }

private:
    omui::ActResult change(double v);
    [[nodiscard]] double at(float x) const noexcept;  // the rating a pointer at x gives

    mutable omui::parts::Motion fill_, hover_;
    mutable double              preview_ = -1;  // where the pointer is: the rating a click gives
};

}  // namespace shelf
