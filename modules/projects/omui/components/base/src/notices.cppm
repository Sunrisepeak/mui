// openmui.ui.base:notices — what a page says about where things stand: an
// alert (a message in the page that needs attention, its tone, one action)
// and a stepper (the steps of a sequence: those done, the current one, those
// ahead; each step an item of the semantic tree, a done one gone back to).
export module omui.ui.base:notices;

import std;
import omui.core;
import omui.view;
import :parts;
import :alert_contract;
import :stepper_contract;

export namespace omui {

class Alert : public AlertContract<Alert> {
public:
    void mounted();
    void prop_changed(AlertProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult activate(ActContext& ctx);
    ActResult dismiss(ActContext& ctx);

private:
    // Where its parts go in a box this wide: the text's width, the action's, the close's.
    [[nodiscard]] float text_width(float width) const;
    void relabel();

    mutable parts::Label title_, message_, action_;
    mutable Rect         action_at_, close_at_;
};

class Stepper : public StepperContract<Stepper> {
public:
    static constexpr bool focusable = true;

    void mounted();
    void prop_changed(StepperProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);

    ActResult set_value(ActContext& ctx, const Value& value);
    ActResult increment(ActContext& ctx);
    ActResult decrement(ActContext& ctx);

    // Whether the step at i may be gone to now (done, or any when free; not the current one).
    [[nodiscard]] bool reachable(std::size_t i) const noexcept;

private:
    ActResult go(std::int64_t to);
    void relabel();
    [[nodiscard]] spec::SemanticNode item(std::string_view path, std::size_t i) const;

    mutable std::vector<parts::Label> labels_, digits_;
    mutable std::vector<Rect>         at_;  // each step (its marker and name) where it was last painted
    int                               hover_ = -1;
};

}  // namespace omui
