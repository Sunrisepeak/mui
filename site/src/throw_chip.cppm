// openmui.site:throw_chip — a spring you can throw (the site plan §7.1, E1):
// a chip on a stage, dragged anywhere and flung; let go, it springs home
// carrying the speed it was let go at — hanim's drive on the host's one
// engine, a glide's velocity handed to a spring at the same instant, so
// nothing jumps. Caught again mid-flight, it is taken from where it is drawn.
export module omui.site:throw_chip;

import std;
import omui.core;
import omui.view;
import omui.ui.base;
import hanim.engine;
import :throw_chip_contract;

export namespace omui::site {

class ThrowChip : public ThrowChipContract<ThrowChip> {
public:
    ~ThrowChip();
    void mounted();
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    bool on_event(EventContext& ctx, const InputEvent& e);

    ActResult fling(ActContext& ctx, const FlingArgs& args);
    ActResult home(ActContext& ctx);

    // Where the chip is from its home (its stage's middle), how fast, and whether it still moves.
    struct Now {
        hanim::Vec2 offset{}, velocity{};
        bool        moving = false, held = false;
    };
    [[nodiscard]] Now now() const;

private:
    [[nodiscard]] hanim::Drive<hanim::Vec2>* drive() const;
    [[nodiscard]] Rect chip(hanim::Vec2 offset) const;  // where the chip is drawn, in the node's box
    void spring_home(hanim::Vec2 velocity);

    mutable std::optional<hanim::Drive<hanim::Vec2>> drive_;
    mutable parts::Label                             label_;
    mutable Size                                     room_;
    bool                                             held_ = false;
    hanim::Vec2                                      grab_{};  // where on the chip it was taken
    // The last moves of a drag (when, where): the speed it is let go at.
    std::array<std::pair<double, hanim::Vec2>, 6> trail_{};
    std::size_t                                   trail_count_ = 0;
};

}  // namespace omui::site
