// openmui.site:drop_card — the home's soft landing (the site plan §5 ③).
//
// A card (an avatar, a title, lines of text, a button), let go over a floor: one soft body of jelly
// (xengine.dynamics' material table, through hanim's Engine::simulate on the
// host's one engine) under the card, everything on it carried by its
// cage. Its time is the body's own (Engine::body_time, seek_body): `at` is
// written as it falls, and set — a slider dragged, an agent's `seek` — it
// goes back to that moment of what it played, drawn exactly as it was; played
// on, the same steps again. Under it a readout says the moment, the step and
// a hash of the body's state at that step: the same each time it comes back.
export module omui.site:drop_card;

import std;
import omui.core;
import omui.view;
import omui.ui.base;
import hanim.engine;
import omui.hanim;
import :drop_card_contract;

export namespace omui::site {

class DropCard : public DropCardContract<DropCard> {
public:
    ~DropCard();
    void mounted();
    void prop_changed(DropCardProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult drop(ActContext& ctx);
    ActResult play(ActContext& ctx);
    ActResult seek(ActContext& ctx, const Value& at);

    // What it is doing now (tests, agents): waiting (not yet let go), falling,
    // resting (played to its end), held (at a moment it was taken back to);
    // the moment, how much it has played, the step and its state's hash.
    struct Now {
        std::string   state = "waiting";
        double        at = 0, played = 0;
        std::int64_t  step = 0;
        std::uint64_t hash = 0;
        Rect          card;  // where the card is drawn (its box's space)
    };
    [[nodiscard]] Now now() const;

private:
    struct Card {
        hanim::Engine*                                      engine = nullptr;
        hanim::ObjectRef                                    object;
        std::optional<hanim::PlaybackRef>                   showing;
        std::optional<hanim::BodyRef>                       body;
        omui::animation::Drawing                            drawing;
        Size                                                room;
        Point                                               origin;  // the card's place in its box, before it falls
    };
    void ensure(const PaintContext& ctx) const;
    void release() const;
    void schedule() const;  // a step at the next frame's start
    void step() const;

    mutable std::unique_ptr<Card> card_;
    mutable std::string           state_ = "waiting";
    mutable bool                  playing_ = false;
    mutable double                last_ = -1;         // when it started playing (the runtime's seconds)
    mutable std::int64_t          from_step_ = 0;     // the step it started playing from
    mutable std::optional<double> wanted_;   // a moment asked for, taken at the next paint
    mutable bool                  dropping_ = false;
    mutable bool                  writing_ = false;  // its own `at` written back: not a seek
    mutable bool                  dark_ = false;     // the theme it was made in
    mutable std::uint64_t         tick_ = 0;
    mutable parts::Label          readout_;
};

}  // namespace omui::site
