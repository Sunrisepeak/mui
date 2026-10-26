// openmui.ui.base:progress — progress and choice among options: a progress
// bar (determinate, or indeterminate while value is negative) and radio
// buttons (one value per group; the group is a signal they share).
export module openmui.ui.base:progress;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :progress_contract;
import :radio_contract;

export namespace omui {

class Progress : public ProgressContract<Progress> {
public:
    void mounted();
    void prop_changed(ProgressProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

private:
    mutable parts::Motion fill_;
};

class Radio : public RadioContract<Radio> {
public:
    void mounted();
    void prop_changed(RadioProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    float baseline(MeasureContext& m, Size content);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult select(ActContext& ctx);
    ActResult activate(ActContext& ctx) { return select(ctx); }

    [[nodiscard]] bool chosen() const noexcept { return !props.value.empty() && props.group == props.value; }

private:
    mutable parts::Label  label_;
    mutable parts::Motion dot_, hover_;
    mutable bool          shown_ = false;
};

}  // namespace omui
