// openmui.ui.base:containers — what holds the application's own content: a
// split view (two panes sharing their room, the share moved by a handle that
// is an item of its own) and a drawer (a sheet along the window's edge, over
// the page).
export module omui.ui.base:containers;

import std;
import omui.core;
import omui.view;
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
    float         past_ = 0;  // the handle drawn past its limit while dragged there (px, damped)
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

    // Dragged toward its edge (B5): where the hand went down, last was and when, how fast it went (px/s, toward the
    // edge), and how far the sheet is drawn toward it (past open: damped).
    struct Drag {
        float  from = 0, last = 0, along = 0;
        double at = 0, velocity = 0;
    };

    mutable parts::Label title_;
    bool                 was_open_ = false;
    std::optional<Drag>  drag_;
    bool                 dragging_ = false;  // moved past the slop: the pointer is the drawer's
};

}  // namespace omui
