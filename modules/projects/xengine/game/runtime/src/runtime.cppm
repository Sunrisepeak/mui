// xengine.runtime — a game running on its own (D-162).
//
// A game is the engine's: its state, its fixed steps, its rules. It runs
// without omui — a server, a thousand playtests, CI — and is a subject of the
// agent kernel (mlibs.agent) from its own data: its state is explicit and
// hashed, what a player is shown is a projection the game writes, an agent
// plays through the same actions a person does, held to a tier (how soon
// after what it saw, how often), and time is the game's steps: stepped,
// sought, branched ahead without touching what is played.
//
//   struct Play {                                   // what a game gives (Playable)
//       using State = …; using Input = …;           // mlibs.simulation's Model
//       static void step(State&, const Input&);
//       static constexpr std::int32_t steps_per_second = 120;
//       static std::uint64_t checksum(const State&);           // the same play, the same number
//       static std::string   observe(const State&, bool full); // what a player is shown, as JSON
//       static void          play(Input&, const xengine::Actions&);  // an agent player's actions into a step's input
//       static bool          over(const State&);
//       // optional (D-186) — what its controls mean, and tasks to begin with, in the game's own words:
//       static constexpr std::array<Control, 2> controls{{{"left", "walk left", .axis = 0, .toward = -1}, {"jump", "leave the ground", .button = 0}}};
//       static constexpr std::array<Move, 1>    jump_once{{{.hold = {"jump"}, .steps = 30}}};
//       static constexpr std::array<Task, 1>    tasks{{{"jump-once", "Jump and land", jump_once}}};
//   };
//   xengine::runtime::Runtime<Play> game(initial);
//   mlibs::agent::Endpoint endpoint({.name = "breakout", .session = [&] {
//       return std::make_unique<mlibs::agent::SubjectSession>(std::vector<mlibs::agent::Subject*>{&game}); }});
//   → {"op":"world.observe"}  → {"op":"world.act","seen":120,"buttons":[0],"axis":[1]}  → {"op":"world.step","steps":12}
//   → {"op":"world.branch","steps":240,"axis":[-1]}     what would be, the game untouched
export module xengine.runtime;

import std;
import openas;
import mlibs.control;
import mlibs.agent;
import mlibs.json;
import mlibs.simulation;
import xengine.input;
export import :catalog;  // the game's operations (world.ops.omui.ts, omui-gen ops)

export namespace xengine::runtime {

template <class G>
concept Playable = mlibs::simulation::Model<G> &&
                   requires(const typename G::State& state, typename G::Input& input, const xengine::Actions& actions, bool full) {
                       { G::steps_per_second } -> std::convertible_to<std::int32_t>;
                       { G::checksum(state) } -> std::convertible_to<std::uint64_t>;
                       { G::observe(state, full) } -> std::convertible_to<std::string>;
                       { G::play(input, actions) };
                       { G::over(state) } -> std::convertible_to<bool>;
                   };

// What a control means (D-186): a button, an axis pushed one way, or the pointer, by the name a player knows it
// by. Without it a request's `buttons: [0]` is a number an agent has to try to learn; with it `world.state` lists
// the controls, each with the `world.act` that holds it, and `find` answers "jump".
struct Control {
    std::string_view name;      // "jump", "left": lower-case words joined by hyphens
    std::string_view summary;   // what it does in the game
    int              button = -1;      // a button's index (0 … 31) …
    int              axis = -1;        // … or an axis (0 … 3)
    int              toward = 0;       //     pushed this way (−1 or 1)
    bool             pointer = false;  // … or the pointer: a place in the game's area
};
// One move of a task: controls held, by name (none: let go of everything), for so many steps.
struct Move {
    std::array<std::string_view, 4> hold{};
    std::int32_t                    steps = 1;
    std::array<double, 2>           pointer{0.5, 0.5};  // where the pointer is, when a pointer control is held
};
// A task to begin with, said by the game: what it gets done, and the moves that do it. The runtime writes each
// move as the requests that play it (`world.act`, `world.step`) and ends with `world.observe`; it is listed by
// `describe` before the runtime's own tasks, found by intent, and run as written by the contract suite.
struct Task {
    std::string_view      name;     // "catch-the-mark"
    std::string_view      summary;
    std::span<const Move> moves;
};

template <class G>
concept SaysControls = requires {
    { G::controls } -> std::convertible_to<std::span<const Control>>;
};
template <class G>
concept SaysTasks = SaysControls<G> && requires {
    { G::tasks } -> std::convertible_to<std::span<const Task>>;
};

// Whether what a game says of itself holds together: each control is one thing with a name of its own, and
// each move holds controls the game has. Checked when the game is compiled (Runtime's static_assert).
[[nodiscard]] constexpr bool told_well(std::span<const Control> controls, std::span<const Task> tasks) noexcept {
    const auto named = [](std::string_view name) {
        if (name.empty() || name.front() == '-' || name.back() == '-') return false;
        for (const char letter : name)
            if (!(letter >= 'a' && letter <= 'z') && !(letter >= '0' && letter <= '9') && letter != '-') return false;
        return true;
    };
    for (std::size_t index = 0; index < controls.size(); ++index) {
        const Control& control = controls[index];
        if (!named(control.name) || control.summary.empty()) return false;
        const int kinds = (control.button >= 0 ? 1 : 0) + (control.axis >= 0 ? 1 : 0) + (control.pointer ? 1 : 0);
        if (kinds != 1 || control.button > 31 || control.axis > 3) return false;
        if (control.axis >= 0 && control.toward != -1 && control.toward != 1) return false;
        for (std::size_t other = 0; other < index; ++other)
            if (controls[other].name == control.name) return false;
    }
    for (std::size_t index = 0; index < tasks.size(); ++index) {
        const Task& task = tasks[index];
        if (!named(task.name) || task.summary.empty() || task.moves.empty()) return false;
        for (std::size_t other = 0; other < index; ++other)
            if (tasks[other].name == task.name) return false;
        for (const Move& move : task.moves) {
            if (move.steps < 1) return false;
            for (const std::string_view held : move.hold) {
                if (held.empty()) continue;
                bool known = false;
                for (const Control& control : controls) known = known || control.name == held;
                if (!known) return false;
            }
        }
    }
    return true;
}

// The members of a `world.act` that holds these controls: `"buttons":[0],"axis":[0,1]` (nothing held: empty).
[[nodiscard]] inline std::string held_as_written(std::span<const Control> controls, std::span<const std::string_view> hold, std::array<double, 2> pointer) {
    std::string buttons;
    std::array<int, 4> axes{};
    int highest = -1;
    bool pointed = false;
    for (const std::string_view held : hold)
        for (const Control& control : controls) {
            if (held.empty() || control.name != held) continue;
            if (control.button >= 0) buttons += std::format("{}{}", buttons.empty() ? "" : ",", control.button);
            if (control.axis >= 0) axes[static_cast<std::size_t>(control.axis)] = control.toward, highest = std::max(highest, control.axis);
            pointed = pointed || control.pointer;
        }
    std::string out;
    const auto member = [&](std::string text) { out += std::format("{}{}", out.empty() ? "" : ",", text); };
    if (!buttons.empty()) member(std::format("\"buttons\":[{}]", buttons));
    if (highest >= 0) {
        std::string values;
        for (int index = 0; index <= highest; ++index) values += std::format("{}{}", index ? "," : "", axes[static_cast<std::size_t>(index)]);
        member(std::format("\"axis\":[{}]", values));
    }
    if (pointed) member(std::format("\"pointer\":[{},{}]", pointer[0], pointer[1]));
    return out;
}

// Who steps the game: its owner's loop (a host's clock, a server's), or the
// agent that plays it (nothing moves between its requests: a playtest, a test).
enum class Stepping : std::uint8_t { owner, agent };

// Who plays the first seat: the agent's lane gives each step its actions, or
// the step's input is played as its owner gives it — a person at a window, a
// recording played again, an owner that lets an agent in through `lane()` by
// its own rules.
enum class Seat : std::uint8_t { agent, owner };

struct Options {
    xengine::PlayTier tier{.name = "human", .reaction_steps = 12, .actions_per_second = 10, .full_view = false};
    Stepping          stepping = Stepping::agent;
    Seat              seat = Seat::agent;
    std::int64_t      keep = 3600;        // steps of history kept, to seek within
    std::int64_t      most_steps = 3600;  // the most one request steps or branches ahead
    // Keep every step's input since the first: what plays this game again anywhere, bit for bit
    // (`recorded()`). Off: only the `keep` steps a seek needs.
    bool              record = false;
};

template <Playable G>
class Runtime final : public mlibs::agent::Subject {
public:
    using State = typename G::State;
    using Input = typename G::Input;

    explicit Runtime(const State& initial, Options options = {})
        : options_(std::move(options)), timeline_(initial, options_.keep), lane_(options_.tier, G::steps_per_second) {}

    // One step: the owner's input for it; where the agent has the seat, its actions of this step put in.
    void step(Input input = {}) {
        if (options_.seat == Seat::agent) G::play(input, lane_.take(timeline_.frame()));
        if (options_.record) {
            recorded_.resize(static_cast<std::size_t>(timeline_.frame()));  // a step after a seek back: what followed is another branch's
            recorded_.push_back(input);
        }
        timeline_.step(input);
        if (stepped_) stepped_(timeline_.state());
    }
    // Who is told what the game's time did: after each step, and after it went back to a frame (a seek, its
    // owner's or a request's). What keeps something beside the game that follows its time — a world's record
    // of causes — is told here; the game knows nothing of it. A step run ahead on a copy (world.branch) tells no one.
    void tell(std::function<void(const State&)> stepped, std::function<void(const State&)> rewound) {
        stepped_ = std::move(stepped), rewound_ = std::move(rewound);
    }
    [[nodiscard]] const State& state() const noexcept { return timeline_.state(); }
    [[nodiscard]] std::int64_t frame() const noexcept { return timeline_.frame(); }
    [[nodiscard]] bool over() const { return G::over(timeline_.state()); }
    [[nodiscard]] const xengine::PlayTier& tier() const noexcept { return lane_.tier(); }

    // ── the owner's own way in (a host's loop, a game's rules for rewinding) ──
    // The history: its frames kept (`earliest()` … `latest()`), each step's input while kept.
    [[nodiscard]] const mlibs::simulation::Timeline<G>& timeline() const noexcept { return timeline_; }
    // Back to a frame kept (false, and nothing changes, outside it). What was played after it stays reachable
    // until `branch()` or the next step.
    bool seek(std::int64_t frame) {
        if (!timeline_.seek(frame)) return false;
        if (rewound_) rewound_(timeline_.state());
        return true;
    }
    // From here on is a new branch: what was played after this frame is forgotten.
    void branch() noexcept {
        timeline_.branch();
        if (options_.record) recorded_.resize(static_cast<std::size_t>(timeline_.frame()));
    }
    // The agent player's lane: an owner that seats the agent by its own rules submits and takes here.
    [[nodiscard]] xengine::AgentLane& lane() noexcept { return lane_; }
    // Every step's input since the first, on this branch (with `Options::record`): played on a game begun
    // from the same state with `Seat::owner`, the same bits.
    [[nodiscard]] std::span<const Input> recorded() const noexcept { return recorded_; }

    // ── the game as a subject ─────────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "world"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.world@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        return {.observation = true, .control = true, .time = true, .causes = false, .preview = true, .history = false};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xengine::runtime::listing; }
    // What a game hosted here is, and the tasks to begin with: the game's own first (G::tasks, each move
    // written as the requests that play it), then the runtime's. Stays as it is until asked again.
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override {
        const std::span<const Task> own = said_tasks();
        if (own.empty()) return xengine::runtime::guide;
        step_texts_.clear();
        std::vector<std::size_t> counts;
        for (const Task& task : own) {
            const std::size_t before = step_texts_.size();
            for (const Move& move : task.moves) {
                const std::string held = held_as_written(said_controls(), move.hold, move.pointer);
                step_texts_.push_back(held.empty() ? std::string(R"({"op":"world.act"})") : std::format("{{\"op\":\"world.act\",{}}}", held));
                step_texts_.push_back(std::format("{{\"op\":\"world.step\",\"steps\":{}}}", move.steps));
            }
            step_texts_.emplace_back(R"({"op":"world.observe"})");
            counts.push_back(step_texts_.size() - before);
        }
        steps_.assign(step_texts_.begin(), step_texts_.end());
        tasks_.clear();
        std::size_t first = 0;
        for (std::size_t index = 0; index < own.size(); ++index) {
            tasks_.push_back({own[index].name, own[index].summary, std::span<const std::string_view>(steps_).subspan(first, counts[index])});
            first += counts[index];
        }
        tasks_.append_range(xengine::runtime::guide.tasks);
        return {xengine::runtime::guide.about, tasks_};
    }
    // What the game names, for `find`: its controls, each with the request that holds it.
    [[nodiscard]] std::vector<mlibs::agent::Term> terms() const override {
        std::vector<mlibs::agent::Term> out;
        for (const Control& control : said_controls())
            out.push_back({"control", std::string(control.name), std::string(control.summary), std::format("{{\"op\":\"world.act\",{}}}", held_alone(control))});
        return out;
    }
    [[nodiscard]] std::uint64_t state_hash() const override { return G::checksum(timeline_.state()); }

    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override {
        if (operation == "world.observe") return answer.data(view(timeline_.state(), timeline_.frame()));
        if (operation == "world.state") return answer.data(status());
        if (operation == "world.act") return act(request, answer);
        if (operation == "world.step") return play(request, answer);
        if (operation == "world.seek") return seek(request, answer);
        if (operation == "world.branch") return branch(request, answer);
    }

private:
    [[nodiscard]] std::string view(const State& state, std::int64_t frame) const {
        return std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"over\":{},\"tier\":{},\"view\":{}}}", frame, G::checksum(state), G::over(state),
                           mlibs::agent::json_text(lane_.tier().name), G::observe(state, lane_.tier().full_view));
    }
    [[nodiscard]] static constexpr std::span<const Control> said_controls() noexcept {
        if constexpr (SaysControls<G>) return G::controls;
        else return {};
    }
    [[nodiscard]] static constexpr std::span<const Task> said_tasks() noexcept {
        if constexpr (SaysTasks<G>) return G::tasks;
        else return {};
    }
    static_assert(told_well(said_controls(), said_tasks()),
                  "what the game says of itself does not hold together: a control is one of a button (0 … 31), an axis (0 … 3) pushed −1 or 1, or the "
                  "pointer, with a name of its own (lower-case words joined by hyphens) and a summary; a task has a name, a summary and moves, each "
                  "holding controls the game has, for one step or more");
    [[nodiscard]] static std::string held_alone(const Control& control) {
        const std::array<std::string_view, 1> hold{control.name};
        return held_as_written(said_controls(), hold, {0.5, 0.5});
    }
    // Where the game's time is; and, where the game says them, what its controls mean — each with the members
    // of the `world.act` that holds it.
    [[nodiscard]] std::string status() const {
        std::string out = std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"over\":{},\"earliest\":{},\"latest\":{}", timeline_.frame(),
                                      G::checksum(timeline_.state()), G::over(timeline_.state()), timeline_.earliest(), timeline_.latest());
        if (const std::span<const Control> controls = said_controls(); !controls.empty()) {
            out += ",\"controls\":[";
            for (std::size_t index = 0; index < controls.size(); ++index)
                out += std::format("{}{{\"name\":{},\"summary\":{},\"act\":{{{}}}}}", index ? "," : "", mlibs::agent::json_text(controls[index].name),
                                   mlibs::agent::json_text(controls[index].summary), held_alone(controls[index]));
            out += ']';
        }
        return out + "}";
    }
    // A player's actions as a request writes them: `buttons` held (their indices), `axis` (−1 … 1 each), `pointer` ([x, y], 0 … 1).
    [[nodiscard]] static std::expected<xengine::Actions, std::string> actions_of(const mlibs::agent::Request& request) { return actions_of(request.document->root()); }
    [[nodiscard]] static std::expected<xengine::Actions, std::string> actions_of(const mlibs::json::Node& request) {
        xengine::Actions actions;
        for (const auto& button : request["buttons"].items()) {
            const auto index = button.integer();
            if (!index || *index < 0 || *index > 31) return std::unexpected(std::string("`buttons` are indices 0 … 31"));
            actions.held |= 1u << static_cast<unsigned>(*index);
        }
        const auto axes = request["axis"].items();
        if (axes.size() > actions.axes.size()) return std::unexpected(std::string("`axis` has at most four values"));
        for (std::size_t index = 0; index < axes.size(); ++index) {
            const auto value = axes[index].number();
            if (!value || !(*value >= -1 && *value <= 1)) return std::unexpected(std::string("an `axis` value is −1 … 1"));
            actions.axes[index] = static_cast<std::int16_t>(*value * 32767.0);
        }
        if (const auto pointer = request["pointer"]) {
            const auto at = pointer.items();
            const auto x = at.size() == 2 ? at[0].number() : std::nullopt;
            const auto y = at.size() == 2 ? at[1].number() : std::nullopt;
            if (!x || !y || !(*x >= 0 && *x <= 1 && *y >= 0 && *y <= 1)) return std::unexpected(std::string("`pointer` is [x, y], each 0 … 1"));
            actions.pointer_x = static_cast<std::int16_t>(*x * 32767.0);
            actions.pointer_y = static_cast<std::int16_t>(*y * 32767.0);
        }
        return actions;
    }

    void act(const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
        auto actions = actions_of(request);
        if (!actions) return answer.fail(openas::error_code::invalid_argument, actions.error());
        // The lane counts steps as they are played (the step that makes frame f + 1 is step f). A request arrives
        // between steps: the latest step played is frame − 1, and a view at frame f showed step f − 1.
        const std::int64_t seen = request["seen"].integer().value_or(timeline_.frame());
        auto due = lane_.submit(timeline_.frame() - 1, seen - 1, *actions);
        if (!due) {
            if (due.error() == xengine::AgentLane::Refusal::unseen)
                return answer.fail(openas::error_code::invalid_argument, std::format("`seen` is step {}, and step {} is the latest played", seen, timeline_.frame()));
            return answer.fail(openas::error_code::refused, std::format("more often than the `{}` tier allows ({} actions a second)", lane_.tier().name,
                                                                         lane_.tier().actions_per_second));
        }
        // The frame from which the state shows it: the step it is first held in, played.
        answer.data(std::format("{{\"effective\":{},\"frame\":{}}}", *due + 1, timeline_.frame()));
    }

    void play(const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
        if (options_.stepping != Stepping::agent)
            return answer.fail(openas::error_code::refused, "this game is stepped by its owner's clock, not by requests");
        const std::int64_t steps = request["steps"].integer().value_or(1);
        if (steps < 1 || steps > options_.most_steps)
            return answer.fail(openas::error_code::invalid_argument, std::format("`steps` is 1 … {}", options_.most_steps));
        std::int64_t played = 0;
        for (; played < steps && !G::over(timeline_.state()); ++played) step();
        answer.data(std::format("{{\"played\":{},\"frame\":{},\"hash\":\"{:016x}\",\"over\":{}}}", played, timeline_.frame(), G::checksum(timeline_.state()),
                                G::over(timeline_.state())));
    }

    void seek(const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
        const auto frame = request["frame"].integer();
        if (!frame) return answer.fail(openas::error_code::invalid_argument, "`frame`: the step to return to");
        if (!seek(*frame))
            return answer.fail(openas::error_code::stale, std::format("step {} is outside the history kept ({} … {})", *frame, timeline_.earliest(), timeline_.latest()));
        branch();
        // What the player held belonged to the steps left behind.
        lane_ = xengine::AgentLane(options_.tier, G::steps_per_second);
        answer.data(status());
    }

    void branch(const mlibs::agent::Request& request, mlibs::agent::Answer& answer) const {
        auto actions = actions_of(request);
        if (!actions) return answer.fail(openas::error_code::invalid_argument, actions.error());
        const std::int64_t steps = request["steps"].integer().value_or(1);
        if (steps < 1 || steps > options_.most_steps)
            return answer.fail(openas::error_code::invalid_argument, std::format("`steps` is 1 … {}", options_.most_steps));
        // What would be if the actions were given now: on a copy of the player's lane, so they take effect as
        // the tier's reaction allows and what is held until then is held — the future `world.act` and
        // `world.step` would play, not one a person could not have.
        xengine::AgentLane lane = lane_;
        if (!lane.submit(timeline_.frame() - 1, timeline_.frame() - 1, *actions))
            return answer.fail(openas::error_code::refused, std::format("more often than the `{}` tier allows ({} actions a second): the action itself would be refused",
                                                                         lane_.tier().name, lane_.tier().actions_per_second));
        // And what would follow it: `then`, each an action given so many steps after the branch begins (a
        // press and its release, a run and a jump) — what a player does next, tried with what it does first.
        struct Later {
            std::int64_t     after;
            xengine::Actions actions;
        };
        std::vector<Later> then;
        for (const auto& one : request["then"].items()) {
            const auto after = one["after"].integer();
            if (!after || *after < 1 || *after >= steps || (!then.empty() && *after <= then.back().after))
                return answer.fail(openas::error_code::invalid_argument, "`then`: each with `after`, a step 1 … steps − 1 of the branch, later than the one before");
            auto later = actions_of(one);
            if (!later) return answer.fail(openas::error_code::invalid_argument, later.error());
            then.push_back({*after, *later});
        }
        State future = timeline_.state();  // a copy: the game is not touched
        std::int64_t played = 0;
        std::size_t next = 0;
        for (; played < steps && !G::over(future); ++played) {
            if (next < then.size() && then[next].after == played) {
                const std::int64_t latest = timeline_.frame() + played - 1;
                if (!lane.submit(latest, latest, then[next].actions))
                    return answer.fail(openas::error_code::refused, std::format("`then` at step {}: more often than the `{}` tier allows ({} actions a second)", played,
                                                                                 lane_.tier().name, lane_.tier().actions_per_second));
                ++next;
            }
            Input input{};
            // Whoever seats the agent for real (the runtime, or an owner by its own rules), what is asked is
            // what its actions would do: they are played here.
            G::play(input, lane.take(timeline_.frame() + played));
            G::step(future, input);
        }
        answer.data(view(future, timeline_.frame() + played));
    }

    Options                        options_;
    mlibs::simulation::Timeline<G> timeline_;
    std::function<void(const State&)> stepped_, rewound_;
    xengine::AgentLane             lane_;
    std::vector<Input>             recorded_;
    // The guide as last asked: the game's tasks as requests, and what views them.
    mutable std::vector<std::string>          step_texts_;
    mutable std::vector<std::string_view>     steps_;
    mutable std::vector<mlibs::control::Task> tasks_;
};

}  // namespace xengine::runtime
