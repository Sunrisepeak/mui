// openmui.ui.base:pickers — a date, a time of day and a colour, each chosen
// the way a person expects and named the way an agent needs: a date picker
// (its popup a month, each day an item of the picker, `<id>/2026-11-05`), a
// time picker (its hours and minutes spin buttons of their own) and a colour
// picker (its popup a saturation–brightness field, a hue strip and the
// application's swatches, each an item). Whatever is chosen is set_value's,
// so a press, a key and an agent's request are taken back the same way.
export module omui.ui.base:pickers;

import std;
import omui.core;
import omui.view;
import :parts;
import :date_picker_contract;
import :time_picker_contract;
import :color_picker_contract;

export namespace omui {

// ISO dates (2026-11-02) and #rrggbb colours, as the pickers read and write them.
namespace picking {
[[nodiscard]] std::optional<std::chrono::year_month_day> parse_date(std::string_view iso);
[[nodiscard]] std::string iso(std::chrono::year_month_day d);
[[nodiscard]] std::string spoken(std::chrono::year_month_day d);  // "5 November 2026"
struct Hsv {
    double h = 0, s = 0, v = 0;  // degrees, 0–1, 0–1
};
[[nodiscard]] std::optional<Color> parse_hex(std::string_view hex);
[[nodiscard]] std::string hex(Color c);
[[nodiscard]] Hsv to_hsv(Color c);
[[nodiscard]] Color from_hsv(Hsv hsv);
}  // namespace picking

class DatePicker : public DatePickerContract<DatePicker> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(DatePickerProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<Element> compose();
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult expand(ActContext& ctx);
    ActResult collapse(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);

    [[nodiscard]] bool expanded() const;
    // Whether a date may be chosen (within min and max).
    [[nodiscard]] bool allowed(std::chrono::year_month_day d) const;

private:
    [[nodiscard]] NodeId calendar() const;
    void open(bool on);
    ActResult choose(std::string iso, bool restoring = false);
    [[nodiscard]] spec::SemanticNode day_item(std::string_view path, std::chrono::year_month_day d) const;

    mutable parts::Label label_;
};

// The month a date picker shows (its popup): drawn, pressed and keyed here;
// what is chosen is the picker's set_value, performed as the person.
class DateCalendar {
public:
    static constexpr std::string_view name = "date_calendar";
    static constexpr Role role = Role::grid;
    static constexpr bool overlay = true;
    static constexpr bool focusable = true;
    [[nodiscard]] Placement placement() const { return {Placement::Side::below, Placement::Anchor::parent, 4, false, false, true, false}; }

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    bool set_prop(std::string_view k, const Value& v);
    [[nodiscard]] Value get_prop(std::string_view k) const;
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    ActResult act(ActContext& ctx, std::string_view action);

    [[nodiscard]] std::chrono::year_month month() const noexcept { return month_; }
    void turn(int months);  // the month shown, moved

private:
    void pick(std::chrono::year_month_day d);
    void close();
    [[nodiscard]] std::chrono::year_month_day first_cell() const;  // the grid's first day (of the week the month starts in)
    [[nodiscard]] Value picker(std::string_view prop) const;

    Services*                          services_ = nullptr;
    NodeId                             node_;
    bool                               open_ = false;
    std::chrono::year_month            month_{std::chrono::year{2026}, std::chrono::month{1}};
    std::chrono::year_month_day        focus_{};
    mutable parts::Label               title_;
    mutable std::array<parts::Label, 7> weekdays_;
    mutable std::array<parts::Label, 42> days_;
    mutable std::array<Rect, 42>        cells_{};
    mutable Rect                        prev_at_, next_at_;
    int                                 hover_ = -1;
};

class TimePicker : public TimePickerContract<TimePicker> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(TimePickerProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult set_value(ActContext& ctx, const Value& value);
    ActResult increment(ActContext& ctx);
    ActResult decrement(ActContext& ctx);
    ActResult previous(ActContext& ctx);
    ActResult next(ActContext& ctx);

    // Minutes since midnight, as the value says (none when it does not parse).
    [[nodiscard]] static std::optional<int> minutes_of(std::string_view hhmm) noexcept;

private:
    ActResult to(int minutes, bool restoring = false);
    ActResult step(int part, int by);
    [[nodiscard]] int now() const noexcept;
    [[nodiscard]] int parts_count() const noexcept { return props.hour12 ? 3 : 2; }
    [[nodiscard]] std::string shown(int part) const;
    [[nodiscard]] spec::SemanticNode item(std::string_view path, int part) const;

    mutable std::array<parts::Label, 4> texts_;  // hours, minutes, period, the colon
    mutable std::array<Rect, 3>         at_{};
    int                                 focus_ = 0;
    std::string                         typed_;  // digits typed into the focused part so far
};

class ColorPicker : public ColorPickerContract<ColorPicker> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(ColorPickerProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<Element> compose();
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult expand(ActContext& ctx);
    ActResult collapse(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);

    [[nodiscard]] bool expanded() const;
    // The hue it is at: the value's, or (grey has none) the last one chosen.
    [[nodiscard]] double hue() const noexcept { return hue_; }
    // Its properties, and its hue as its panel reads it (`hue_now`).
    [[nodiscard]] Value get_prop(std::string_view k) const {
        return k == "hue_now" ? Value(hue_) : ColorPickerContract<ColorPicker>::get_prop(k);
    }

private:
    [[nodiscard]] NodeId panel() const;
    void open(bool on);
    ActResult take(std::string hex, std::optional<double> hue = std::nullopt);
    void picking(const std::string& hex, double hue);  // a drag under way: shown and said, not yet one action
    [[nodiscard]] spec::SemanticNode item(std::string_view path, std::string_view key) const;

    mutable parts::Label label_;
    double               hue_ = 0;
    std::string          drag_from_;  // a drag's start, taken back whole
};

// A colour picker's popup: the saturation–brightness field, the hue strip and
// the swatches; dragged and pressed here, said to the picker.
class ColorPanel {
public:
    static constexpr std::string_view name = "color_panel";
    static constexpr Role role = Role::dialog;
    static constexpr bool overlay = true;
    static constexpr bool focusable = true;
    [[nodiscard]] Placement placement() const { return {Placement::Side::below, Placement::Anchor::parent, 4, false, false, true, false}; }

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    bool set_prop(std::string_view k, const Value& v);
    [[nodiscard]] Value get_prop(std::string_view k) const;
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    ActResult act(ActContext& ctx, std::string_view action);

    // Its parts, in its own coordinates.
    [[nodiscard]] static Rect field_rect() noexcept;
    [[nodiscard]] static Rect hue_rect() noexcept;
    [[nodiscard]] Rect swatch_rect(std::size_t i) const noexcept;

private:
    void drag_to(Point local);
    [[nodiscard]] Value picker(std::string_view prop) const;

    Services* services_ = nullptr;
    NodeId    node_;
    bool      open_ = false;
    int       dragging_ = 0;  // 0 none, 1 the field, 2 the hue strip
};

}  // namespace omui
