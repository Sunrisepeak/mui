// openmui.ui.base:containers — what holds the application's own content: a
// split view (two panes sharing their room, the share moved by a handle that
// is an item of its own) and a drawer (a sheet along the window's edge, over
// the page).
export module openmui.ui.base:containers;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :split_view_contract;
import :drawer_contract;

export namespace omui {

class SplitView : public SplitViewContract<SplitView> {
public:
    static constexpr bool focusable = true;

    SplitView() = default;
    SplitView(const SplitView&) = delete;
    SplitView& operator=(const SplitView&) = delete;
    ~SplitView();

    void mounted();
    void prop_changed(SplitViewProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
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

    // A share within its bounds (min of either edge).
    [[nodiscard]] double kept(double ratio) const noexcept;

private:
    void apply();                      // its panes' shares of the room
    void change(double ratio);         // shown and said; not yet an action
    ActResult to(double ratio, bool exact = false);
    [[nodiscard]] Rect handle() const; // between the panes, in its own coordinates
    [[nodiscard]] std::vector<NodeId> panes() const;

    std::uint64_t timer_ = 0;
    bool          hover_ = false;
    bool          dragging_ = false;
    double        from_ = 0;  // a drag's start, taken back whole
};

class Drawer : public DrawerContract<Drawer> {
public:
    static constexpr bool overlay = true;
    [[nodiscard]] Placement placement() const;

    void mounted();
    void prop_changed(DrawerProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult dismiss(ActContext& ctx);

private:
    [[nodiscard]] Rect close_box(Size size) const;

    mutable parts::Label title_;
    bool                 was_open_ = false;
};

}  // namespace omui
