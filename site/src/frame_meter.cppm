// openmui.site:frame_meter — who draws the page and how fast, in the bar.
export module openmui.site:frame_meter;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.base;
import :frame_meter_contract;

export namespace omui::site {

// What the meter shows, from the frames the host showed.
struct FrameReading {
    std::string           renderer;  // as the host says: "cpu", "webgpu", "webgl"…
    double                fps = 0;   // the last second of the frames that ran on together
    std::optional<double> cost_ms;   // their median cost; none where the host does not measure
    std::size_t           frames = 0;  // how many ran on together (the last run)
    bool                  idle = true;  // nothing shown for a while: nothing moved
};

[[nodiscard]] FrameReading read_frames(const ShownFrames& frames, std::string_view renderer);

class FrameMeter : public FrameMeterContract<FrameMeter> {
public:
    void prop_changed(FrameMeterProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw, layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult reading(ActContext& ctx);

    // How long after the last frame shown the page counts as at rest.
    static constexpr double rest = 0.3;
    // The bars: the last frames, one each.
    static constexpr std::size_t bars = 36;

private:
    mutable parts::Label  device_, rate_, cost_;
    mutable std::uint64_t timer_ = 0;  // the frame a moment after the last, to say the page is at rest
    std::shared_ptr<int>  life_ = std::make_shared<int>(0);  // the timer's: it does nothing once the meter is gone
};

}  // namespace omui::site
