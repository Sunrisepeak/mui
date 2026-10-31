// openmui.ui.base:select — one value from a popup list (role combo_box). A
// composite component: it builds its own popup (an overlay at least as wide
// as the field) and an option per value; choosing one writes `value` back,
// emits `changed`, and closes the popup. Agents choose with set_value.
export module openmui.ui.base:select;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :select_contract;

export namespace omui {

class Select : public SelectContract<Select> {
public:
    void mounted();
    void prop_changed(SelectProp p);
    void style(const Theme& theme, layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    [[nodiscard]] std::vector<Element> compose();

    ActResult expand(ActContext& ctx);
    ActResult collapse(ActContext& ctx);
    ActResult set_value(ActContext& ctx, const Value& value);

    [[nodiscard]] std::string label_of(std::string_view value) const;
    [[nodiscard]] bool expanded() const;
    void choose(const std::string& value);

private:
    [[nodiscard]] NodeId popup() const;
    void open(bool on);

    mutable parts::Label  label_;
    mutable parts::Motion hover_;
};

// The popup a select builds (an overlay below it).
class SelectPopup {
public:
    static constexpr std::string_view name = "select_popup";
    static constexpr Role role = Role::list_box;
    static constexpr bool overlay = true;
    [[nodiscard]] Placement placement() const { return {Placement::Side::below, Placement::Anchor::parent, 4, false, false, true, true}; }

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    bool set_prop(std::string_view k, const Value& v);
    [[nodiscard]] Value get_prop(std::string_view k) const;
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    ActResult act(ActContext& ctx, std::string_view action);

private:
    Services* services_ = nullptr;
    NodeId    node_;
    bool      open_ = false;
};

// An option of a select's popup.
class SelectOption {
public:
    static constexpr std::string_view name = "select_option";
    static constexpr Role role = Role::option;
    static constexpr bool focusable = true;

    void mount(Services& s, NodeId n) { services_ = &s, node_ = n; }
    bool set_prop(std::string_view k, const Value& v);
    [[nodiscard]] Value get_prop(std::string_view k) const;
    void style(const Theme& theme, layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    [[nodiscard]] bool selected() const;

private:
    Services*            services_ = nullptr;
    NodeId               node_;
    std::string          value_;
    mutable parts::Label label_;
};

}  // namespace omui
