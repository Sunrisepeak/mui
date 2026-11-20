// omui.generate:replay — the design shown in place (G8, D-253): how an
// application was designed — a replay (mlibs.agent's replay@1, which
// design.history.export writes from the design's history) — played in the
// running application, not as a film: each step's script texts run in place
// of its scripts' (as an edit runs, its changes presented as an edit's are),
// under the step's caption and a timeline of how far it has come, its maker shown at
// what the step built (a note at the step's node: the agent's presence, D-160);
// at the end the application's own scripts again. A person (its play button,
// the timeline they scrub, Close) or an agent (generate.replay) plays, pauses,
// goes to a step and stops; every step shown is a real interface. Its course
// can lie on a node of the application's own (`course`: a path's id, the
// site's O): where the step's interface has it, the steps are on that shape
// where it is drawn, and the card has no line of its own.
//
//     omui::generate::Replayer replayer(runtime, *mlibs::agent::read_replay(text),
//         [&](const auto& texts) { return player.show(texts); }, [&] { player.discard_edits(); });
//     replayer.play(1);
export module omui.generate:replay;

import std;
import omui.core;
import omui.view;
import mlibs.agent;

export namespace omui::generate {

// A step's texts run in place of the scripts' (the player's show), or why not.
using ShowTextsFn = std::function<std::expected<void, std::string>(const std::map<std::string, std::string>& texts)>;

class Replayer {
public:
    // `restore`: the application's own scripts again (the player's edits dropped). `course`: the id of a path its
    // course lies on where a step's interface has one (omui.config.ts's generation.replay.course), else the card's.
    Replayer(Runtime& runtime, mlibs::agent::Replay replay, ShowTextsFn show, std::function<void()> restore, std::string course = {});
    ~Replayer();
    Replayer(const Replayer&) = delete;
    Replayer& operator=(const Replayer&) = delete;

    // Plays from step `from` (1: the first), each step its seconds divided by `speed` (¼ to 4).
    std::expected<void, std::string> play(std::size_t from = 1, double speed = 1);
    void pause();
    // Shows step `step` (1 …) and stays there.
    std::expected<void, std::string> at(std::size_t step);
    // The application as it ships again, the caption gone.
    void stop();

    // The steps (caption, who, seconds) and where it is: `at` 0 is the application as it ships.
    [[nodiscard]] Value state() const;
    [[nodiscard]] std::size_t step() const noexcept { return at_; }
    [[nodiscard]] bool playing() const noexcept { return timer_ != 0; }
    [[nodiscard]] const mlibs::agent::Replay& replay() const noexcept { return replay_; }

    // The slot its caption is mounted at, over the application.
    static constexpr std::string_view slot = "generate.replay";

private:
    std::expected<void, std::string> show(std::size_t step);
    void schedule();
    void present();
    void place_course();  // on the course's node where the step's interface has it

    Runtime&                 runtime_;
    mlibs::agent::Replay     replay_;
    ShowTextsFn              show_;
    std::function<void()>    restore_;
    std::size_t              at_ = 0;       // the step shown (0: none)
    double                   speed_ = 1;
    std::uint64_t            timer_ = 0;    // the next step's (0: paused)
    bool                     ended_ = false;
    std::string              problem_;      // why the last step could not be shown
    Signal<std::string>      caption_{std::string()}, who_{std::string()};
    Signal<double>           shown_{0.0};   // the step shown, on the person's timeline (they scrub it)
    Signal<std::string>      toggle_{std::string("Pause")};  // its play button's word
    std::string              course_;       // the id of the path its course lies on (empty: the card's line)
    Signal<std::string>      over_{std::string()}, course_shape_{std::string()}, course_box_{std::string()};
    Signal<bool>             on_node_{false};  // the step's interface has that path
    std::uint64_t            looking_ = 0;     // a look for it again (a page shown when its evaluation settles)
    int                      looks_ = 0;
    std::set<std::string>    noted_;        // the makers whose notes it put up (withdrawn at the end)
    bool                     mounted_ = false;
    void                     said_playing();
};

}  // namespace omui::generate
