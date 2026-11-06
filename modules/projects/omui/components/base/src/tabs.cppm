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
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    // A bar's surfaces, for a style's effect (liquid glass): its capsule and the lens under the selected tab.
    std::size_t effect_shapes(Size size, Rect content, std::span<render::EffectShape> out) const;

    ActResult next(ActContext& ctx);
    ActResult previous(ActContext& ctx);

    // The selected tab: by `selected`, else the first.
    [[nodiscard]] NodeId current() const;
    // Selects a tab as a person does: the bound value learns it, `changed` says it.
    void choose(NodeId tab, bool focus);

private:
    ActResult step(int by);
    [[nodiscard]] Rect under() const;          // the selected tab, in its own space
    [[nodiscard]] Rect lens(Size size) const;  // a bar's lens, under the selected tab

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
    mutable double hover_ = 0;
};

}  // namespace omui
