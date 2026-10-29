// openmui.site:timebar — the page's recent time along the bottom of the
// site: what happened when, and a playhead dragged through it.
export module openmui.site:timebar;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.base;
import :timebar_contract;

export namespace omui::site {

class Timebar : public TimebarContract<Timebar> {
public:
    void prop_changed(TimebarProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw, layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult increment(ActContext& ctx);
    ActResult decrement(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);

    // A tenth of a second: what a key or an agent's step moves the playhead.
    static constexpr double step = 0.1;

private:
    // The playhead to `t` (kept within the strip), written back and announced.
    void change(double t);
    [[nodiscard]] float x_of(double t, float width) const noexcept;
    // The strip's ends as they are now: live, it ends at the page's time.
    [[nodiscard]] double first() const;
    [[nodiscard]] double last() const;

    mutable std::vector<parts::Label> labels_;  // the marks' captions, reused
    mutable Size size_{};                       // the last painted size: pointer positions map onto it
    bool         dragging_ = false;
};

}  // namespace omui::site
