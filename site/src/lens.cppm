// openmui.site:lens — the semantic tree's wireframe over the page.
export module openmui.site:lens;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.base;
import :lens_contract;

export namespace omui::site {

class Lens : public LensContract<Lens> {
public:
    void prop_changed(LensProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw, layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

private:
    mutable std::vector<parts::Label> roles_;  // each box's role, reused
};

}  // namespace omui::site
