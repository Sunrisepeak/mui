// omui.ui.base:path — a vector shape from SVG path data (G5): a mark, an
// icon, a drawing, scaled from its view box into its own (the ratio kept,
// centred), stroked and filled in the theme's colours, its stroke drawn in
// part by `trim`. It draws where it is; how one shape becomes another is the
// style's (D-93).
export module omui.ui.base:path;

import std;
import omui.core;
import omui.view;
import :path_contract;

export namespace omui {

class Path : public PathContract<Path> {
public:
    void prop_changed(PathProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw,
                 mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    // The data's view box (x y width height), or the default 0 0 100 100 when it is not one.
    [[nodiscard]] Rect view_box() const noexcept;
    // Whether its data is a path (an empty `d` draws nothing and is not a mistake).
    [[nodiscard]] bool drawable() const;

private:
    const mrender::Path* shape() const;  // its data as a path, parsed once a change

    mutable std::optional<mrender::Path> shape_;
    mutable std::string                  shape_of_;  // the data it was parsed from
};

}  // namespace omui
