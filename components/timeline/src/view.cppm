// openmui.ui.timeline:view — the timeline editor's behaviour.
export module openmui.ui.timeline:view;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.base;
import openmui.timeline;
import :timeline_editor_contract;

export namespace omui {

class TimelineEditor : public TimelineEditorContract<TimelineEditor> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(TimelineEditorProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw, layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult seek(ActContext& ctx, const Value& frame);
    ActResult select(ActContext& ctx, const SelectArgs& args);
    ActResult move(ActContext& ctx, const MoveArgs& args);
    ActResult trim(ActContext& ctx, const TrimArgs& args);
    ActResult split(ActContext& ctx);
    ActResult remove(ActContext& ctx, const Value& ripple);
    ActResult revert(ActContext& ctx, const Value& change);

    // The timeline as it is now (after the edits), and what its document said was wrong (empty: nothing).
    [[nodiscard]] const timeline::Timeline& model() const noexcept { return model_; }
    [[nodiscard]] const std::vector<std::string>& problems() const noexcept { return problems_; }
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> selection() const noexcept { return selected_; }

    // The layout, in the node's coordinates: the ruler's height, a track's, the headers' width.
    static constexpr float ruler = 26, track_height = 46, header = 112;

private:
    void read();
    ActResult edit(std::expected<timeline::Change, std::string> change);
    void changed();
    [[nodiscard]] float x_of(timeline::Flicks t) const noexcept;
    [[nodiscard]] timeline::Flicks time_at(float x) const noexcept;
    [[nodiscard]] timeline::Flicks snapped(timeline::Flicks t) const noexcept;
    [[nodiscard]] std::optional<std::size_t> track_at(float y) const noexcept;
    void set_frame(std::int64_t frame);

    // A drag in progress: what it moves (a clip, one of its ends, the playhead) and from where.
    enum class Grip : std::uint8_t { none, clip, start, end, playhead };
    struct Drag {
        Grip              grip = Grip::none;
        std::size_t       track = 0, clip = 0;
        float             from_x = 0, from_y = 0;
        timeline::Flicks  shift = 0;       // how far the pointer has moved, in time, snapped to frames
        std::size_t       to_track = 0;
    };

    timeline::Timeline                 model_;
    std::vector<std::string>           problems_;
    // The inverses of the edits made, by the id the window's history holds (S-12): each made once, then
    // replaced by its own inverse; a document read again clears them (and the window's history).
    std::map<std::int64_t, timeline::Change> kept_;
    std::int64_t                       next_ = 1;
    std::string                        last_;  // the last edit made, undone or redone
    std::optional<std::pair<std::size_t, std::size_t>> selected_;
    Drag                               drag_;
    float                              scroll_ = 0;  // seconds off the left edge
    Size                               size_{};
    mutable std::vector<parts::Label>  names_;   // each clip's, in track order
    mutable std::vector<parts::Label>  tracks_;  // each track's
    mutable std::vector<parts::Label>  seconds_; // the ruler's numbers
};

}  // namespace omui
