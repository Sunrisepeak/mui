// openmui.ui.base:status — what something is and how it stands, small and
// still: a badge (a label, a count, a dot in a tone), an avatar (a picture,
// else initials on the name's own colour, a status dot) and a skeleton
// (content's shape while it comes, busy to every reader). They draw what
// they are now; how a change of them moves is the slot's (universal motion).
export module omui.ui.base:status;

import std;
import omui.core;
import omui.view;
import :parts;
import :badge_contract;
import :avatar_contract;
import :skeleton_contract;

export namespace omui {

class Badge : public BadgeContract<Badge> {
public:
    void mounted();
    void prop_changed(BadgeProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    float baseline(MeasureContext& m, Size content);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    // Its surface for an effect: the pill.
    std::size_t effect_shapes(Size size, Rect content, std::span<xrender::EffectShape> out) const;

    // What it shows: the count (max+ past max), else the text.
    [[nodiscard]] std::string shown() const;
    // Its colours in a theme: the pill and the label.
    [[nodiscard]] static std::pair<Color, Color> colors(const Theme& theme, BadgeTone tone);

private:
    mutable parts::Label label_;
};

class Avatar : public AvatarContract<Avatar> {
public:
    void mounted();
    void prop_changed(AvatarProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    std::size_t effect_shapes(Size size, Rect content, std::span<xrender::EffectShape> out) const;

    // The initials of a name: the first letters of its first and last words ("Ada Lovelace": AL); one word, its
    // first two letters' first (Ada: A); a name in a script without case (林黛玉): its first character.
    [[nodiscard]] static std::string initials(std::string_view name);
    // The name's own colour: the same name, the same hue, in any theme (FNV-1a of it).
    [[nodiscard]] static Color hue_of(std::string_view name, bool dark);

private:
    mutable parts::Label initials_;
};

class Skeleton : public SkeletonContract<Skeleton> {
public:
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    // Its bones, for an effect (a style's shimmer reads them).
    std::size_t effect_shapes(Size size, Rect content, std::span<xrender::EffectShape> out) const;

private:
    // The bones' rectangles in a size.
    std::size_t bones(Size size, std::span<Rect> out) const;
};

}  // namespace omui
