// Swarm — a small top-down shooter, as a game the runtime plays (xengine.runtime's Playable).
//
// The player moves with two axes, aims with the pointer and shoots with a button; enemies arrive in waves from
// one side of the arena and chase the player; a bullet that hits an enemy removes it and scores; an enemy that
// touches the player costs a life; with no life left the game is lost. A pure function of the state and each
// step's actions, 120 steps a second: the state is plain data (xengine.world's block of entities and a few
// numbers), so a copy is a branch and a timeline rewinds and replays it bit for bit.
//
//   swarm::Standalone game(swarm::start(), {.keep = 600});
//   mlibs::agent::SubjectSession session({&game});      // world.observe, world.act, world.step, world.branch …
export module omui.showcase.swarm.headless;

import std;
export import xengine.runtime;
export import xengine.input;
export import xengine.world;
export import mlibs.math;

export namespace swarm {

using mlibs::math::Vec2;

inline constexpr std::int32_t steps_per_second = 120;
// The arena, in its own units: x to the right, y down, from its top-left corner. The pointer's [0, 1] × [0, 1]
// is the whole of it.
inline constexpr float arena_width = 96, arena_height = 54;
inline constexpr float player_radius = 2, enemy_radius = 2, bullet_radius = 0.6f;
inline constexpr float player_speed = 30, bullet_speed = 120;  // units a second
inline constexpr std::int32_t reload_steps = 12;   // between two bullets while the button is held
inline constexpr std::int32_t safe_steps = 90;     // after a touch, nothing costs a life
inline constexpr std::int32_t spawn_steps = 40;    // between two enemies of a wave
inline constexpr std::int32_t rest_steps = 120;    // between a wave cleared and the next
inline constexpr std::int32_t first_lives = 3;
inline constexpr int shoot_button = 0;

// How fast a wave's enemies are (units a second; always slower than the player), how many it has, what one is worth.
[[nodiscard]] constexpr float enemy_speed(std::uint32_t wave) noexcept { return wave >= 12 ? 26.0f : 14.0f + static_cast<float>(wave); }
[[nodiscard]] constexpr std::int32_t wave_size(std::int32_t wave) noexcept { return wave >= 19 ? 40 : 2 + 2 * wave; }
[[nodiscard]] constexpr std::int32_t worth(std::uint32_t wave) noexcept { return 10 * static_cast<std::int32_t>(wave); }

// What is in the arena besides the player: bodies that are an enemy or a bullet.
struct Body {
    Vec2 at{}, velocity{};
    static constexpr bool no_padding = true;  // four floats
};
struct Enemy {
    std::uint32_t wave = 0;  // the wave it came with
};
struct Bullet {
    std::uint32_t age = 0;  // steps flown
};
using Field = xengine::World<128, Body, Enemy, Bullet>;

// The side a wave comes from.
enum class Side : std::uint32_t { east, south, west, north };
[[nodiscard]] constexpr std::string_view name(Side side) noexcept {
    constexpr std::array<std::string_view, 4> names{"east", "south", "west", "north"};
    return names[static_cast<std::size_t>(side)];
}

struct State {
    std::int64_t  frame = 0;
    Field         field{};
    Vec2          player{arena_width / 2, arena_height / 2};
    Vec2          aim{1, 0};            // a unit vector: where the last pointer was, from the player
    std::int32_t  lives = first_lives, score = 0;
    std::int32_t  wave = 0;             // the wave arriving or last cleared (0: none yet)
    std::int32_t  to_come = 0;          // enemies of this wave not yet in the arena
    std::int32_t  spawn_in = 0;         // steps until the next of them
    std::int32_t  rest = steps_per_second / 2;  // steps of quiet left before the next wave
    std::int32_t  enemies = 0;          // in the arena now
    std::int32_t  reload = 0;           // steps until the next bullet can leave
    std::int32_t  safe_for = 0;         // steps for which a touch costs nothing
    std::int32_t  shots = 0, kills = 0;
    Side          from = Side::east;    // where this wave comes from (the first: always the east)
    std::uint64_t random = 0;           // the stream that places enemies and picks sides (mlibs.math's SplitMix64)
};
struct Input {
    xengine::Actions player{};
};

// A new game: the player in the middle, three lives, the first wave half a second away. The same seed, the same game.
[[nodiscard]] State start(std::uint64_t seed = 1) noexcept;
// One step of 1/120 s.
void step(State& state, const Input& input) noexcept;
[[nodiscard]] std::uint64_t checksum(const State& state) noexcept;
[[nodiscard]] constexpr bool over(const State& state) noexcept { return state.lives <= 0; }
// What a player is shown, as JSON: the arena, the player, every enemy and bullet, the score, lives and wave,
// and a legend of what the names and numbers mean. The full view adds what is not drawn: the random stream.
[[nodiscard]] std::string observe(const State& state, bool full);
// A place of the arena as the pointer says it (a share of the arena in 16-bit steps), for an owner's input.
[[nodiscard]] constexpr std::pair<std::int16_t, std::int16_t> pointer_at(Vec2 place) noexcept {
    const auto share = [](float value, float whole) {
        const float part = value / whole;
        return static_cast<std::int16_t>((part < 0 ? 0.0f : part > 1 ? 1.0f : part) * 32767.0f);
    };
    return {share(place.x, arena_width), share(place.y, arena_height)};
}

struct Play {
    using State = swarm::State;
    using Input = swarm::Input;
    static constexpr std::int32_t steps_per_second = swarm::steps_per_second;
    static void step(State& state, const Input& input) noexcept { swarm::step(state, input); }
    [[nodiscard]] static std::uint64_t checksum(const State& state) noexcept { return swarm::checksum(state); }
    [[nodiscard]] static std::string observe(const State& state, bool full) { return swarm::observe(state, full); }
    static void play(Input& input, const xengine::Actions& actions) noexcept { input.player = actions; }
    [[nodiscard]] static bool over(const State& state) noexcept { return swarm::over(state); }

    // The rules a player is told before playing (a cold start at the game had to infer each of them from previews).
    static constexpr std::string_view about =
        "Swarm, a top-down shooter: enemies arrive in waves from one side of the arena and walk straight at the player; one bullet removes an enemy; an "
        "enemy that touches the player costs one of three lives and is gone. Wave n has 2 + 2n enemies (at most 40), each worth 10n, a little faster than "
        "the wave before and always slower than the player; the first wave comes from the east, the side of each later one is not known until it "
        "arrives. There is no winning: the waves go on, and the game ends when the lives are gone";
    // What the controls mean, and what to begin with (D-186): `world.state` lists them, `find` answers "shoot".
    static constexpr std::array<xengine::runtime::Control, 6> controls{{
        {.name = "left", .summary = "Move the player left (toward x = 0)", .axis = 0, .toward = -1},
        {.name = "right", .summary = "Move the player right", .axis = 0, .toward = 1},
        {.name = "up", .summary = "Move the player up (toward y = 0, the top of the arena)", .axis = 1, .toward = -1},
        {.name = "down", .summary = "Move the player down", .axis = 1, .toward = 1},
        {.name = "shoot", .summary = "Fire toward `aim`; held, a bullet leaves every tenth of a second", .button = shoot_button},
        {.name = "aim", .summary = "Where the gun points: a place in the arena, [x, y], each 0 to 1 of it ([0, 0] the top-left corner); it stays aimed that way",
         .pointer = true},
    }};
    static constexpr std::array<xengine::runtime::Move, 2> hold_the_gate{{{.hold = {"aim", "shoot"}, .steps = 600, .pointer = {1.0, 0.5}}, {.steps = 1}}};
    static constexpr std::array<xengine::runtime::Move, 2> sidestep{{{.hold = {"left", "up"}, .steps = 120}, {.steps = 12}}};
    static constexpr std::array<xengine::runtime::Move, 1> stand{{{.steps = 600}}};
    static constexpr std::array<xengine::runtime::Task, 3> tasks{{
        {"hold-the-east-gate", "From a new game: aim east and hold fire for five seconds — the first wave comes from the east and is shot as it closes in; see the score",
         hold_the_gate},
        {"sidestep", "Move up and left for a second, let go, and see where the player stands", sidestep},
        {"stand-and-be-caught", "From a new game: do nothing for five seconds — the first wave reaches the player and a life is lost", stand},
    }};
};

using Standalone = xengine::runtime::Runtime<Play>;

}  // namespace swarm
