// openmui.ui.base:inputs — what a person gives: how much out of a few stars
// (rating), a one-time code a character a box (otp input), a control with
// its label, its help and its error (form field), words to search with
// (search box) and a few options each on or off (toggle group). Each is what
// an agent drives the same way, through its actions and its items.
export module omui.ui.base:inputs;

import std;
import omui.core;
import omui.view;
import :parts;
import :rating_contract;
import :otp_input_contract;
import :form_field_contract;
import :search_box_contract;
import :toggle_group_contract;
import :file_drop_contract;

export namespace omui {

class Rating : public RatingContract<Rating> {
public:
    static constexpr bool focusable = true;

    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult increment(ActContext& ctx);
    ActResult decrement(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);

    // The value a press at `x` (local) sets: the star under it, or its half.
    [[nodiscard]] double value_at(float x) const;

private:
    ActResult rate(double value);
    [[nodiscard]] double step() const { return props.half ? 0.5 : 1.0; }
    std::optional<double> preview_;  // what a press would set, while the pointer is over it
};

class OtpInput : public OtpInputContract<OtpInput> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(OtpInputProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult set_value(ActContext& ctx, const Value& value);

    // What of `text` a box can hold: digits, or letters and digits (upper case).
    [[nodiscard]] static std::string accepted(std::string_view text, bool numeric);

private:
    ActResult take(std::string code);
    void typed(std::string_view text);
    void rebuild();
    mutable std::vector<parts::Label> cells_;
    std::size_t                       caret_ = 0;  // the box a character goes into
};

class FormField : public FormFieldContract<FormField> {
public:
    void mounted();
    void prop_changed(FormFieldProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

private:
    [[nodiscard]] float label_height(const Theme& theme) const;
    [[nodiscard]] float note_height(const Theme& theme) const;
    mutable parts::Label label_, note_;
};

class SearchBox : public SearchBoxContract<SearchBox> {
public:
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    [[nodiscard]] std::vector<Element> compose();
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult set_value(ActContext& ctx, const Value& value);
    ActResult clear(ActContext& ctx);
    ActResult submit(ActContext& ctx);

private:
    ActResult words(std::string value);
    [[nodiscard]] Rect clear_box(Size size) const;
};

class ToggleGroup : public ToggleGroupContract<ToggleGroup> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(ToggleGroupProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult set_selected(ActContext& ctx, const Value& selected);
    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);

private:
    ActResult choose(std::vector<std::string> selected);
    [[nodiscard]] bool on(std::string_view option) const;
    [[nodiscard]] std::string label_of(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view option) const;
    [[nodiscard]] spec::SemanticNode item(std::string_view path, std::size_t index) const;
    mutable std::vector<parts::Label> labels_;
    mutable std::vector<Rect>         at_;
    int                               hover_ = -1;
    std::size_t                       focus_ = 0;  // the option the keys move between
};

class FileDrop : public FileDropContract<FileDrop> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(FileDropProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult drop(ActContext& ctx, const Value& files);
    ActResult activate(ActContext& ctx);

    // Whether it takes a file by its extension (`accept`; none: any).
    [[nodiscard]] bool accepts(std::string_view file) const;

private:
    mutable parts::Label label_, note_;
    bool                 over_ = false;  // files are over it now
    std::string          refused_;      // what the last drop left out, said
};

}  // namespace omui
