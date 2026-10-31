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
import gongfu.ai;
import :stage_contract;
import :roster;
import :hud;
import :sound;

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
    ActResult rematch(ActContext& ctx);
    ActResult leave(ActContext& ctx);
    ActResult fighters(ActContext& ctx, const FightersArgs& args);
    ActResult demonstrate(ActContext& ctx, const Value& move);
    ActResult pause(ActContext& ctx);
    ActResult resume(ActContext& ctx);
    ActResult step(ActContext& ctx);
    ActResult press(ActContext& ctx, const PressArgs& args);
    ActResult release(ActContext& ctx, const Value& player);
    ActResult turn(ActContext& ctx, const TurnArgs& args);
    ActResult inputs(ActContext& ctx);
    ActResult save(ActContext& ctx);
    ActResult load(ActContext& ctx);
    ActResult record(ActContext& ctx, const Value& player);
    ActResult replay(ActContext& ctx, const Value& player);
    ActResult stop(ActContext& ctx, const Value& player);
    ActResult fill(ActContext& ctx);
    ActResult demo_frame(ActContext& ctx, const Value& frame);

    // The match being played, and training's readings of it (tests and tools read them).
    [[nodiscard]] const gongfu::Match& match() const noexcept { return *match_; }
    [[nodiscard]] const gongfu::training::Readout& readout() const noexcept { return readout_; }
    // What the picture keeps of the match: its contacts (where they were drawn), recent poses.
    [[nodiscard]] const gongfu::present2d::Effects& effects() const noexcept { return effects_; }
    // What the fight sounds like: a device pulls its mixer (the app's), or a test renders it offline.
    [[nodiscard]] Sounds& sounds() noexcept { return sounds_; }

private:
    void schedule();
    void tick();
    void advance();  // one frame: the dummies, the match, its effects, the readings
    void start_demo();  // the `demo` move from its beginning (none: the match as it was)
    void demo_from_start();  // the demonstration's first frame again: its match, readings and effects
    void announce();  // the round over: the announcer's calls
    void changed();  // repaint, and tell agents
    void start_round(bool next = false);  // a new match (next: its next round, the rounds won kept)
    [[nodiscard]] std::int32_t to_win() const noexcept;
    [[nodiscard]] gongfu::Pad keyboard(std::int32_t player) const;
    [[nodiscard]] gongfu::Pad gamepad(std::int32_t player) const;
    [[nodiscard]] gongfu::Pad pad_of(std::int32_t player) const;
    bool on_gamepad(const GamepadEvent& g);

    // The card over the match: paused (Esc, a pad's start: the match stands)
    // or the results (the match decided, its K.O. seen). Its items by what
    // they do: resume, restart, rematch, fighters.
    enum class Menu : std::uint8_t { none, paused, results, moves };
    [[nodiscard]] Menu menu() const noexcept;
    [[nodiscard]] std::span<const std::string_view> menu_items() const noexcept;
    bool on_menu(EventContext& ctx, const InputEvent& e);  // the card's keys, pad and pointer
    void choose(std::string_view item);
    void open_menu(bool paused);
    bool                                  paused_ = false;   // the pause card is up
    bool                                  swapped_ = false;  // training: the person plays player 2 (recording it from the card)
    // Training's move list (from the pause card): player 1's moves by name, and as they read (name, startup, damage); ten a page.
    bool                                  listing_ = false;
    std::vector<std::string_view>         moves_;
    std::vector<std::string>              move_labels_;
    void list_moves();
    [[nodiscard]] std::pair<std::size_t, std::size_t> page() const noexcept;  // the items shown: the first, how many
    mutable std::array<std::string_view, 10> items_{};        // the card's items as they stand (menu_items)
    void save_state();
    bool load_state();
    void fill_meters();
    bool                                  fresh_ = false;    // left for the select screen: the next play is a new match
    std::int32_t                          menu_at_ = 0;      // its lit item

    std::unique_ptr<gongfu::Match>        match_;
    // Training's demonstration of a move (`demo`): its match at the move's
    // beginning, its pads, and the frame of it being played.
    std::optional<gongfu::training::Demo> demo_;
    std::int32_t                          demo_at_ = 0;
    bool                                  quiet_ = false;  // frames played to reach a frame (demo_frame): not heard
    static constexpr std::int32_t         demo_rest = 40;  // frames at rest before it plays again
    std::size_t                           left_ = 0, right_ = 0;  // the fighters, by their place in the roster
    // The two fighters' looks (the left's first palette, the right's second),
    // their pictures registered with this stage's renderer (art_: by file).
    std::array<gongfu::present2d::Look, 2> looks_;
    // The stage's art with its pictures registered (none: the plain room).
    std::optional<gongfu::present2d::Stage> stage_;
    std::shared_ptr<const Registered>     art_;
    gongfu::present2d::Effects            effects_;
    std::set<Key>                         held_;
    // The players' gamepads: the first two connected, each in the seat it took.
    struct Gamepad {
        std::optional<std::uint32_t>             device;
        std::array<bool, gamepad_buttons>        held{};
        float                                    x = 0, y = 0;  // the left stick
    };
    std::array<Gamepad, 2>                    gamepads_{};
    std::array<std::optional<gongfu::Pad>, 2> agent_{};
    std::array<std::optional<gongfu::ai::Bot>, 2> bots_{};  // the computer's player (props.cpu)
    std::array<bool, 2>                       turned_{};      // turns: who has said what they hold
    std::array<std::vector<gongfu::Pad>, 2>   turn_pads_{};   // and what: a pad a frame, the last held
    std::array<std::string, 2>                seats_{};       // turns: the actor who plays each seat (the first to)
    Value                                     last_turn_;     // what happened in the last turn played
    std::vector<std::array<gongfu::Pad, 2>>   inputs_;        // the round's pads, frame by frame (`inputs`)
    std::int64_t                              turns_ = 0;     // turns played
    void make_bots();
    gongfu::training::Readout             readout_;
    Sounds                                sounds_;
    std::shared_ptr<void>                 heard_;  // the program's device on sounds_ (let go first)
    std::array<gongfu::training::Recorder, 2> dummies_{};
    struct Saved {
        gongfu::State              state;
        gongfu::present2d::Effects effects;
        std::vector<std::array<gongfu::Pad, 2>> inputs;  // the pads that had led to it (a round restarted since, they still do)
        std::size_t                left = 0, right = 0;  // between whom (another pair cannot take it)
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
    // Versus is played in rounds (best of three by default): the round, each side's rounds won, the frame this one ended on.
    std::int32_t                          round_ = 1;
    std::array<std::int32_t, 2>           wins_{};
    std::int32_t                          over_for_ = 0;  // frames played since this round ended (the match stands still then)
    bool                                  decided_ = false;  // the match is over (its last round's K.O.)
};

}  // namespace omui::fighting
