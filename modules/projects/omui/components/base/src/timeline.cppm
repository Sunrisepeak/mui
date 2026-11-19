// omui.ui.base:timeline — a course of steps on a shape of its own (G8,
// D-253): a line, a ring, or any path given as SVG path data in a 1×1 box.
// The steps lie along it at equal lengths (a ring's last one before its
// first again); pressed or dragged, the shape's nearest point takes the
// nearest step; keys and actions move it as a slider's value. Its parts —
// the track, the part played, each step's mark, the playhead — are drawn
// where they are; how they move is the style's (D-93).
export module omui.ui.base:timeline;

import std;
import omui.core;
import omui.view;
import :parts;
import :timeline_contract;

export namespace omui {

class Timeline : public TimelineContract<Timeline> {
public:
    static constexpr bool draws_disabled = true;  // its own disabled state (faded)
    void mounted();
    void prop_changed(TimelineProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw,
                 mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult increment(ActContext& ctx);
    ActResult decrement(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);

    // Its course in its own coordinates (laid on a box of `size`): the points along its shape, each one's length from
    // the start, and whether it closes (a ring). Empty when the shape is not a path.
    struct Course {
        std::vector<Point> points;
        std::vector<float> lengths;  // from the start, one a point
        bool               closed = false;
        [[nodiscard]] float total() const noexcept { return lengths.empty() ? 0.0f : lengths.back(); }
    };
    [[nodiscard]] const Course& course(Size size) const;
    // Where step `step` lies (1 … steps; between two steps, between them), and the step nearest a point.
    [[nodiscard]] Point point_at(double step, Size size) const;
    [[nodiscard]] double step_at(Point local, Size size) const;
    // The steps, two at least.
    [[nodiscard]] std::int64_t count() const noexcept { return std::max<std::int64_t>(2, props.steps); }

private:
    bool change(double step);
    [[nodiscard]] float fraction_of(double step) const noexcept;  // its length along, as a fraction of the course's

    mutable Course      course_;
    mutable Size        course_size_{};
    mutable std::string course_shape_;
    mutable Size        size_{};
    bool                dragging_ = false;
    double              drag_from_ = 1;
};

}  // namespace omui
