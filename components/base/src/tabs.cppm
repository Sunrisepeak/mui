// openmui.ui.base:tabs — a tab strip (role tab_list) whose tabs select one
// panel. `selected` is the selected tab's id (bindable); the indicator
// springs under it; arrow keys move along the strip.
export module openmui.ui.base:tabs;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :tabs_contract;
import :tab_contract;

export namespace omui {

class Tabs : public TabsContract<Tabs> {
public:
    void mounted();
    void prop_changed(TabsProp p);
    void style(const Theme& theme, layout::Style& s) const;
    void paint_over(PaintContext& ctx) const;

    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);

    // The selected tab: by `selected`, else the first.
    [[nodiscard]] NodeId current() const;
    // Selects a tab as a person does: the bound value learns it, `changed` says it.
    void choose(NodeId tab, bool focus);

private:
    ActResult step(int by);

    mutable parts::Motion x_, w_;
    mutable bool          shown_ = false;
};

class Tab : public TabContract<Tab> {
public:
    void mounted();
    void prop_changed(TabProp p);
    void style(const Theme& theme, layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult select(ActContext& ctx);
    ActResult activate(ActContext& ctx) { return select(ctx); }

    [[nodiscard]] bool selected() const;

private:
    mutable parts::Label  label_;
    mutable parts::Motion hover_;
};

}  // namespace omui
