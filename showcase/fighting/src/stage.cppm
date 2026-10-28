// The stage: a gongfu match played at 60 frames a second from the keyboard
// or an agent, drawn by gongfu.present2d into the node's box, with the
// health and the timer and, in training, every box, the frame meter, the
// input history, the readings, a saved state and a recording dummy.
export module openmui.showcase.fighting:stage;

import std;
import openmui.core;
import openmui.view;
import gongfu.present2d;
import gongfu.training;
import :stage_contract;
import :roster;
import :hud;

export namespace omui::fighting {

class Stage : public StageContract<Stage> {
public:
    // The keyboard reaches it: a game reads its own keys.
    static constexpr bool focusable = true;

    Stage() = default;
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;
    ~Stage();

    void mounted();
    void prop_changed(StageProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw, layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult restart(ActContext& ctx);
    ActResult pause(ActContext& ctx);
    ActResult resume(ActContext& ctx);
    ActResult step(ActContext& ctx);
    ActResult press(ActContext& ctx, const Value& pad, const Value& player);  // the contract's arguments, by name
    ActResult release(ActContext& ctx, const Value& player);
    ActResult save(ActContext& ctx);
    ActResult load(ActContext& ctx);
    ActResult record(ActContext& ctx, const Value& player);
    ActResult replay(ActContext& ctx, const Value& player);
    ActResult stop(ActContext& ctx, const Value& player);

    // The match being played, and training's readings of it (tests and tools read them).
    [[nodiscard]] const gongfu::Match& match() const noexcept { return *match_; }
    [[nodiscard]] const gongfu::training::Readout& readout() const noexcept { return readout_; }

private:
    void schedule();
    void tick();
    void advance();  // one frame: the dummies, the match, its effects, the readings
    void changed();  // repaint, and tell agents
    void start_round();
    [[nodiscard]] gongfu::Pad keyboard(std::int32_t player) const;
    [[nodiscard]] gongfu::Pad pad_of(std::int32_t player) const;

    std::unique_ptr<gongfu::Match>        match_;
    std::size_t                           left_ = 0, right_ = 0;  // the fighters, by their place in the roster
    gongfu::present2d::Effects            effects_;
    std::set<Key>                         held_;
    std::array<std::optional<gongfu::Pad>, 2> agent_{};
    gongfu::training::Readout             readout_;
    std::array<gongfu::training::Recorder, 2> dummies_{};
    struct Saved {
        gongfu::State              state;
        gongfu::present2d::Effects effects;
    };
    std::optional<Saved>                  saved_;
    // What painting reuses from frame to frame (paint is const; these are its scratch).
    mutable gongfu::present2d::Painter    painter_;
    mutable render::DisplayList           frame_;
    mutable hud::Words                    words_;
    FrameDemandLease                      pace_;
    double                                start_ = 0;   // the clock's time of frame 0 of the playing (seconds)
    std::int64_t                          played_ = 0;  // frames played by the clock since start_
    std::uint64_t                         timer_ = 0;
    bool                                  over_reported_ = false;
};

}  // namespace omui::fighting
