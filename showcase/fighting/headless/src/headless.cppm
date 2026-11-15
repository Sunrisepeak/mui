// The fight as a game the runtime plays (xengine.runtime's Playable): a match between two of the work's fighters,
// the seat's player on the left and gongfu.ai's computer on the right, at 60 frames a second.
//
// The state is the match (gongfu's one block of plain data), which fighters, and the computer's own memory (what it
// has seen, what it plans, its random stream): a copy is a branch, a timeline rewinds and replays it bit for bit. The
// seat's actions become a pad — the first two axes the stick (up jumps, down crouches), buttons 0 to 5 the punches
// and kicks — so an agent plays through `world.act` as a person plays through a pad, on the person's tier.
//
//   fighting::Standalone game(*fighting::start("ni", "mo"));
//   mlibs::agent::SubjectSession session({&game});      // world.observe, world.act, world.step, world.branch …
export module omui.showcase.fighting.headless;

import std;
export import gongfu;
export import gongfu.ai;
export import xengine.runtime;
export import xengine.input;
export import :gongfu_fighters;

export namespace fighting {

inline constexpr std::int32_t steps_per_second = 60;

// The fighters the game carries, by name (the files' names: kata, ke, kun, li, mo, ni, yue).
[[nodiscard]] std::vector<std::string_view> names();

struct State {
    gongfu::State                  match{};
    std::int32_t                   left = 0, right = 0;  // the fighters, by their place in `names()`
    std::uint64_t                  seed = 1;             // the computer's (its choices come from it)
    std::optional<gongfu::ai::Bot> computer;             // the right side's player
};
struct Input {
    xengine::Actions player{};
};

// A new match: `left` (the seat's) against `right` (the computer's), by name; or what is wrong with them.
[[nodiscard]] std::expected<State, std::string> start(std::string_view left, std::string_view right, std::uint64_t seed = 1);
// One frame.
void step(State& state, const Input& input);
// The match's own hash (gongfu's, over its bytes) with which fighters: the computer's memory is decided by them.
[[nodiscard]] std::uint64_t checksum(const State& state) noexcept;
[[nodiscard]] constexpr bool over(const State& state) noexcept { return state.match.over != 0; }
// The pad the seat's actions make.
[[nodiscard]] gongfu::Pad pad_of(const xengine::Actions& actions) noexcept;
// What a player is shown, as JSON: the clock, and each fighter's name, health, meter, place, what it is doing and
// whether it can act, with a legend of what they mean. The full view adds what is not shown: speeds, stun, posture.
[[nodiscard]] std::string observe(const State& state, bool full);

struct Play {
    using State = fighting::State;
    using Input = fighting::Input;
    static constexpr std::int32_t steps_per_second = fighting::steps_per_second;
    static void step(State& state, const Input& input) { fighting::step(state, input); }
    [[nodiscard]] static std::uint64_t checksum(const State& state) noexcept { return fighting::checksum(state); }
    [[nodiscard]] static std::string observe(const State& state, bool full) { return fighting::observe(state, full); }
    static void play(Input& input, const xengine::Actions& actions) noexcept { input.player = actions; }
    [[nodiscard]] static bool over(const State& state) noexcept { return fighting::over(state); }

    static constexpr std::string_view about =
        "A one-round fight, 2D: the seat's fighter on the left against the computer's on the right, 60 frames a second, 99 seconds. A strike that "
        "lands takes health; holding back (away from the other) while it strikes blocks it, standing for high and mid, crouching for low; a "
        "fighter whose health runs out is knocked out and the round is the other's; when the clock runs out the one with more health wins. "
        "Moves are asked for as on a pad: a direction and a button (light, medium, heavy; punch or kick), some after a motion (down, down-forward, "
        "forward). The view says each fighter's place along the stage, what it is doing and whether it can act now";
    static constexpr std::array<xengine::runtime::Control, 10> controls{{
        {.name = "left", .summary = "Hold left: walk left (back, while the other is to the right: it guards)", .axis = 0, .toward = -1},
        {.name = "right", .summary = "Hold right: walk right (toward the other, at the start)", .axis = 0, .toward = 1},
        {.name = "up", .summary = "Jump (with left or right: a jump that way)", .axis = 1, .toward = -1},
        {.name = "down", .summary = "Crouch (with back: a crouching guard, which blocks low strikes)", .axis = 1, .toward = 1},
        {.name = "light-punch", .summary = "A light punch: the fastest strike, short", .button = 0},
        {.name = "medium-punch", .summary = "A medium punch", .button = 1},
        {.name = "heavy-punch", .summary = "A heavy punch: slow, far, hurts most", .button = 2},
        {.name = "light-kick", .summary = "A light kick", .button = 3},
        {.name = "medium-kick", .summary = "A medium kick", .button = 4},
        {.name = "heavy-kick", .summary = "A heavy kick: slow, far", .button = 5},
    }};
    static constexpr std::array<xengine::runtime::Move, 2> walk_in{{{.hold = {"right"}, .steps = 60}, {.steps = 1}}};
    static constexpr std::array<xengine::runtime::Move, 4> walk_in_and_jab{{{.hold = {"right"}, .steps = 90}, {.steps = 2}, {.hold = {"light-punch"}, .steps = 2}, {.steps = 30}}};
    static constexpr std::array<xengine::runtime::Move, 1> guard{{{.hold = {"left"}, .steps = 180}}};
    static constexpr std::array<xengine::runtime::Task, 3> tasks{{
        {"walk-in", "From a new match: walk toward the other for a second, let go, and see how far apart the two stand", walk_in},
        {"walk-in-and-jab", "Walk in for a second and a half, then a light punch: see whether it landed (the other's health) or was blocked", walk_in_and_jab},
        {"guard", "Hold back for three seconds while the computer comes in: its strikes are blocked, a blocked one taking a little at most (see your health)", guard},
    }};
};

using Standalone = xengine::runtime::Runtime<Play>;

}  // namespace fighting
