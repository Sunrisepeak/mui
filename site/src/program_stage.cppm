// openmui.site:program_stage — a work of the motion page (the site plan §7.1):
// a hanim object playing a recipe on the host's one engine (paused, its
// position the work's time), drawn by the node (omui.hanim's Drawing). `at`
// is written as it plays and, set — a slider dragged, an agent's `seek` —
// it goes there and holds: every moment drawn the same each time.
export module omui.site:program_stage;

import std;
import omui.core;
import omui.view;
import hanim.engine;
import omui.hanim;
import :program_stage_contract;

export namespace omui::site {

// A work: its object made for a stage of `size` in a theme, and the recipe
// it plays (on the object made). None for a name the site has no work by.
struct Work {
    hanim::ObjectDescription                         object;
    std::function<hanim::Program(hanim::ObjectRef)> program;
};
// A work's tuning, where it takes one: seconds before it begins and how much it overshoots (0 to 1);
// a recipe's parameter (`amount`, a pair's `amount_y`: the recipe's own unless `tuned`), its
// duration and an idle loop's period (seconds; 0: its own).
struct WorkTuning {
    double delay = 0, bounce = 0;
    bool   tuned = false;
    double amount = 0, amount_y = 0, duration = 0, period = 0;
    friend bool operator==(const WorkTuning&, const WorkTuning&) = default;
};
[[nodiscard]] std::optional<Work> make_work(std::string_view name, Size size, const Theme& theme, WorkTuning tuning = {});

class ProgramStage : public ProgramStageContract<ProgramStage> {
public:
    ~ProgramStage();
    void prop_changed(ProgramStageProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult play(ActContext& ctx);
    ActResult pause(ActContext& ctx);
    ActResult seek(ActContext& ctx, const Value& at);
    ActResult retime(ActContext& ctx, const RetimeArgs& args);

    // Its length (seconds): the recipe's duration (0 before it is made).
    [[nodiscard]] double length() const;

private:
    struct Made {
        hanim::Engine*                    engine = nullptr;
        hanim::ObjectRef                  object;
        std::optional<hanim::PlaybackRef> playback;
        omui::animation::Drawing          drawing;
        Size                              room;
        bool                              dark = false;
        std::string                       work;
        WorkTuning                        tuning;
        double                            length = 0;
        std::string                       plan;  // each piece's start and end, as hanim planned it
    };
    void ensure(const PaintContext& ctx) const;
    void release() const;
    void schedule() const;  // a step at the next frame's start
    void step();
    void write(double at, bool playing);  // its props set and written back (bound or not)

    mutable std::unique_ptr<Made> made_;
    mutable double                from_at_ = 0, from_time_ = 0;  // where and when it started playing
    mutable bool                  writing_ = false;
    mutable std::uint64_t         tick_ = 0;
    [[nodiscard]] WorkTuning tuning() const;
};

}  // namespace omui::site
