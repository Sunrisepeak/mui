// openmui.ui.base:menu — a popup list of commands, and its items. The menu
// is an overlay below its parent (the button that opens it); `open` is
// bindable; Escape, a press outside, or choosing an item closes it, and
// focus goes back to where it came from.
export module openmui.ui.base:menu;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :menu_contract;
import :menu_item_contract;

export namespace omui {

class Menu : public MenuContract<Menu> {
public:
    static constexpr bool overlay = true;
    // Below the element before it (the button that opens it), else below its parent.
    [[nodiscard]] Placement placement() const { return {Placement::Side::below, Placement::Anchor::previous, 4, false, false, true, false}; }

    void mounted();
    void prop_changed(MenuProp p);
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult dismiss(ActContext& ctx);

    // Closes it, as Escape or a press outside does.
    void close();

private:
    void opened();
    [[nodiscard]] NodeId trigger() const;

    mutable parts::Motion open_;
    bool                  was_open_ = false;
};

class MenuItem : public MenuItemContract<MenuItem> {
public:
    void mounted();
    void prop_changed(MenuItemProp p);
    void style(const Theme& theme, layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult activate(ActContext& ctx);

private:
    mutable parts::Label label_, shortcut_;
};

}  // namespace omui
