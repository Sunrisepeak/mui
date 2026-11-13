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
    [[nodiscard]] std::string status() const {
        return std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"over\":{},\"earliest\":{},\"latest\":{}}}", timeline_.frame(),
                           G::checksum(timeline_.state()), G::over(timeline_.state()), timeline_.earliest(), timeline_.latest());
    }
    // A player's actions as a request writes them: `buttons` held (their indices), `axis` (−1 … 1 each), `pointer` ([x, y], 0 … 1).
    [[nodiscard]] static std::expected<xengine::Actions, std::string> actions_of(const mlibs::agent::Request& request) {
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
        State future = timeline_.state();  // a copy: the game is not touched
        std::int64_t played = 0;
        for (; played < steps && !G::over(future); ++played) {
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
};

}  // namespace xengine::runtime
