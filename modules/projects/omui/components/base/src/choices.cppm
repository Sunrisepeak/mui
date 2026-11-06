// openmui.ui.base:choices — small choices: a segmented control (one of a few
// side by side), a chip (pressed, a filter turned on and off, a tag removed)
// and a range slider (two thumbs on one track). What each holds is an item of
// the semantic tree an agent finds and operates (`<id>/<value>`,
// `<id>/remove`, `<id>/low`, `<id>/high`), and each change of value is taken
// back as one action.
export module openmui.ui.base:choices;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :segmented_contract;
import :chip_contract;
import :range_slider_contract;

export namespace omui {

class Segmented : public SegmentedContract<Segmented> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(SegmentedProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult set_value(ActContext& ctx, const Value& value);
    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);

private:
    ActResult choose(std::string value);
    [[nodiscard]] std::string label_of(std::size_t i) const;
    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view value) const;
    [[nodiscard]] spec::SemanticNode item(std::string_view path, std::size_t i) const;

    mutable std::vector<parts::Label> labels_;
    mutable std::vector<Rect>         at_;  // each segment where it was last painted
    int                               hover_ = -1;
};

class Chip : public ChipContract<Chip> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(ChipProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult activate(ActContext& ctx);
    ActResult remove(ActContext& ctx);

private:
    [[nodiscard]] Rect remove_box(Size size) const;

    mutable parts::Label label_;
    mutable Size         size_{};
    bool                 hover_remove_ = false;
};

class RangeSlider : public RangeSliderContract<RangeSlider> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(RangeSliderProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult set_range(ActContext& ctx, const SetRangeArgs& args);
    ActResult increment(ActContext& ctx);
    ActResult decrement(ActContext& ctx);

    // A value on the range's steps, within it.
    [[nodiscard]] double snap(double v) const noexcept;

private:
    // Moves one thumb (0 low, 1 high) to v, the other kept on its side; whether it moved.
    bool move(int thumb, double v, bool commit);
    ActResult step(int thumb, double by);
    [[nodiscard]] Rect track(Size size) const noexcept;
    [[nodiscard]] float x_of(double v, Size size) const noexcept;
    [[nodiscard]] spec::SemanticNode item(std::string_view path, int thumb) const;

    mutable Size size_{};
    int          focus_ = 0;     // the thumb the keys move
    int          dragging_ = -1;
    double       from_low_ = 0, from_high_ = 0;  // a drag's start, to take it back as one action
};

}  // namespace omui
