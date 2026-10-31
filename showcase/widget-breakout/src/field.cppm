// The field: the breakout over a room of real components. Its children are
// the room's bricks (composed from the room, placed by layout); on its first
// step it reads where layout put each into the simulation, and from then on
// every step's events change the bricks for real (a switch flips, a
// capacitor's slider fills and empties, a shutter opens, a broken brick goes
// from the screen and the semantic tree), drawing the paddles, balls,
// circuits, beams and their sparks over them. The pointer or the arrows
// move the paddle, Space or a click launches along the aim, R held rewinds
// (for sparks), X scans, 1–4 direct the copilot, Tab opens the inspector;
// an agent does all of it through the actions. It keeps the run: the score,
// balls and sparks carried from room to room and the contraptions chosen
// between them. A rewind puts every brick back as the simulation had it.
export module openmui.showcase.widget_breakout:field;

import std;
import openmui.core;
import openmui.view;
import mgame.input;
import mgame.ui;
import openmui.showcase.widget_breakout.sim;
import :field_contract;
import :rooms;
import :sound;
import :fx;

export namespace omui::breakout {

// What a run carries from room to room.
struct Run {
    std::int64_t      score = 0;
    std::int32_t      lives = 3, sparks = 0, best = 0;  // best: the longest chain banked
    std::vector<Gift> kit;                              // the contraptions chosen, in order
};

class Field : public FieldContract<Field> {
public:
    static constexpr bool focusable = true;

    Field();
    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;
    ~Field();

    void mounted();
    void prop_changed(FieldProp p);
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    std::vector<Element> compose();

    ActResult launch(ActContext& ctx);
    ActResult aim(ActContext& ctx, const Value& across);
    ActResult move_to(ActContext& ctx, const Value& x);
    ActResult release(ActContext& ctx);
    ActResult pause(ActContext& ctx);
    ActResult resume(ActContext& ctx);
    ActResult step(ActContext& ctx);
    ActResult restart(ActContext& ctx);
    ActResult again(ActContext& ctx);
    ActResult rewind(ActContext& ctx, const Value& frames);
    ActResult scan(ActContext& ctx);
    ActResult order(ActContext& ctx, const Value& to);
    ActResult guard_to(ActContext& ctx, const GuardToArgs& args);
    ActResult set_rules(ActContext& ctx, const SetRulesArgs& args);
    ActResult choose(ActContext& ctx, const Value& gift);
    ActResult next(ActContext& ctx);

    // What it sounds like (tests render it offline).
    [[nodiscard]] Sounds& sounds() noexcept { return *sounds_; }
    // The simulation as it stands (tests and tools read it); none before its first step.
    [[nodiscard]] const State* state() const noexcept { return timeline_ ? &timeline_->state() : nullptr; }
    // The room as the simulation began it (its bricks where layout put them): what bots play.
    [[nodiscard]] const Level& level() const noexcept { return level_; }
    [[nodiscard]] const Run& run_so_far() const noexcept { return run_; }
    // The contraptions' rules as last handed over: every room's from its start.
    [[nodiscard]] const Rules& rules() const noexcept { return rules_; }
    // Between rooms: the two contraptions offered (none: not choosing).
    [[nodiscard]] std::optional<std::array<Gift, 2>> offered() const;
    // The frame a rewind reaches back to now, at most (none: it cannot).
    [[nodiscard]] std::optional<std::int64_t> reach() const;

private:
    void run(std::int32_t steps);
    bool begin();                  // the simulation from the bricks' places; false until layout has placed them
    void play(const Input& in);    // one step, and its events on the bricks
    void show_bricks();            // every brick as the simulation has it (after a rewind)
    void show_brick(std::size_t i, const Brick* b);  // one (none: broken)
    [[nodiscard]] Input input();
    [[nodiscard]] std::vector<NodeId> bricks() const;  // the field's bricks, in order (none in a canvas room)
    void note(std::string change);  // a change the semantic tree saw, for the inspector
    void refresh();                 // paint and semantics again
    void restart_room();            // the room afresh, from what the run has now
    void new_run();
    void show_style();              // asks for the room's when it changed
    void show_gates();              // a gate's bar fills toward its opening; open, it is gone
    void show_camera();             // a canvas room: the canvas's camera where the simulation's is
    void over_step();               // a room over: the celebration, then the choice
    bool seek_back(std::int64_t to);  // a rewind's seek, the bricks after it
    void end_rewind();              // let go: what it cost, from where
    [[nodiscard]] bool dark() const;  // room 4 with its lights off
    [[nodiscard]] bool scanning() const;

    static constexpr std::size_t log_size = 8;

    std::optional<mgame::Timeline<Game>> timeline_;
    Level                                level_;
    mgame::Sampler                       sampler_;
    mgame::ui::Pacer                     pacer_;
    Run                                  run_, run_at_start_;   // now, and as the room began
    Rules                                rules_;                // the contraptions' (their scripts'), for every room
    std::optional<Rules>                 pending_rules_;        // to the simulation at the next step's start
    std::int32_t                         pending_pay_ = 0;      // a rewind's cost, on the next step
    std::optional<double>                agent_x_, agent_aim_;  // an agent's paddle (0 … 1) while it holds it; its aim, once
    bool                                 agent_launch_ = false;
    bool                                 rewinding_ = false;    // R held: back two frames a step
    std::int64_t                         rewind_from_ = 0, landing_ = 0;  // where it began; the last one's landing (none goes before it)
    std::int64_t                         scan_until_ = 0, scan_ready_ = 0;
    std::array<bool, 128>                discovered_{};         // hidden caches seen
    std::string                          order_ = "feed";       // room 5: the copilot's (it starts by feeding you)
    Gift                                 fresh_ = Gift::none;   // the contraption just chosen: a taste of it as the next room begins
    std::optional<double>                guard_x_;              // room 5: where an agent sends the top paddle (0 … 1)
    std::string                          says_;                 // its last word, shown a moment
    std::int64_t                         says_until_ = 0;
    std::optional<double>                intent_;               // where it means to send the ball (field units)
    std::deque<std::string>              agent_log_;            // its latest actions, with what it saw (the inspector)
    bool                                 choosing_ = false;
    std::int32_t                         over_steps_ = 0;       // steps since the room was over
    std::uint32_t                        hints_ = 0;            // shown already, by bit
    std::string                          hint_;                 // the one showing, until then
    std::int64_t                         hint_until_ = 0;
    std::string                          style_shown_;          // what the host was last asked for
    Fx                                   fx_;
    std::unique_ptr<Sounds>              sounds_ = std::make_unique<Sounds>();
    std::shared_ptr<void>                device_;               // what hears them (none headless); let go before them
    std::deque<std::string>              log_;                  // the inspector's latest changes
    struct Words {                                              // text shaped once, by what it is
        mutable std::unordered_map<std::uint64_t, typeset::Paragraph> shaped;
    };
    Words                                words_;
    mutable render::Path                 path_;                 // a frame's paths, built in one buffer
};

}  // namespace omui::breakout
