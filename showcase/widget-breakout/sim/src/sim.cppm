// The breakout's simulation (S4): a pure function of the state and the
// frame's actions, 120 steps a second.
//
//     omui::breakout::State s = omui::breakout::start(level);   // bricks from the level's layout
//     omui::breakout::step(s, {player, agent});                   // one frame
//     for (std::uint32_t i = 0; i < s.events; ++i) draw(s.event[i]);   // what happened in it
//
// A frame: each paddle moves (the axis, or toward the pointer); a ball on a
// paddle follows it until launched; each ball moves from one collision to
// the next (walls, paddles by their relative motion, bricks), at most eight a
// frame, hits at one instant ordered by index and their normals combined (a
// seam between two bricks bounces like a face); a brick hit loses durability
// and, broken, scores by the combo and may drop a capsule; capsules fall and
// are caught; a ball out of the field is lost, the last one a life. All in
// one block of plain data: a timeline rewinds it, a checksum compares it.
export module openmui.showcase.widget_breakout.sim;

import std;
export import mgame.world;    // its state's types are these packages' (worlds, actions, vectors)
export import mgame.input;
export import mgame.physics;
export import mgame.sim;     // its Game is a timeline's

export namespace omui::breakout {

inline constexpr double field_w = 1280, field_h = 720;  // a screen of the field (a level may be taller: its height)
inline constexpr std::int32_t steps_per_second = 120;

// What a brick is (its component in the UI), and what a capsule gives.
// A gate is a wall that stands open for a while, once (level 4: only a rewind gets through it again).
enum class Kind : std::uint32_t { button, toggle, slider, card, wall, hidden, gate };
enum class Gift : std::uint32_t { none, wide, multi, slow, pierce };

struct Body {
    mgame::Box box;
    static constexpr bool no_padding = true;
};
struct Brick {
    Kind          kind = Kind::button;
    std::int32_t  durability = 1;
    std::uint32_t id = 0;   // the UI's brick (its node)
    std::uint32_t row = 0;
    Gift          gift = Gift::none;  // what it always drops, broken (none: now and then, by chance)
};
struct Ball {
    mgame::V2     at, velocity;  // velocity: pixels a step
    double        radius = 8;
    std::uint32_t on = 0;        // on a paddle (its index + 1), waiting to be launched; 0: flying
    std::uint32_t pierce = 0;    // steps left of passing through bricks
    static constexpr bool no_padding = true;
};
struct Capsule {
    mgame::V2     at;
    Gift          gift = Gift::none;
    std::uint32_t spare = 0;
    static constexpr bool no_padding = true;
};

using World = mgame::World<512, Body, Brick, Ball, Capsule>;

// What the gifts do: numbers a level sets, or scripts (level 6) — each
// bounded, all whole (ticks, pixels, percents, counts), in the state, so a
// rewind brings back the rules in force then; changed only at a step's start,
// by the step's input (a replay applies them again without any script).
struct Rules {
    std::int32_t wide_steps = 10 * steps_per_second, wide_half = 110;  // a wide paddle: how long, half how wide
    std::int32_t multi_balls = 2;                                        // a flying ball's extra balls (1 … 4: ±20°, then ±40°)
    std::int32_t slow_steps = 8 * steps_per_second, slow_percent = 50;  // slow motion: how long, the clock's speed
    std::int32_t pierce_steps = 5 * steps_per_second;                  // passing through bricks: how long
    std::int32_t drop_one_in = 6;                                        // a broken brick drops a capsule one time in this many
    friend constexpr bool operator==(const Rules& a, const Rules& b) noexcept {  // written out (GCC 16)
        return a.wide_steps == b.wide_steps && a.wide_half == b.wide_half && a.multi_balls == b.multi_balls && a.slow_steps == b.slow_steps &&
               a.slow_percent == b.slow_percent && a.pierce_steps == b.pierce_steps && a.drop_one_in == b.drop_one_in;
    }
};
// Each within its bounds: wide 1–30 s and 80–300 px half, 1–4 balls, slow 1–20 s at 25–90 %, pierce 1–20 s, one in 2–20.
[[nodiscard]] Rules bounded(Rules r) noexcept;

struct Paddle {
    double       x = field_w / 2, y = field_h - 30;
    double       half = 70;      // half its width
    double       moved = 0;      // this step's movement (for the relative sweep)
    std::int32_t wide = 0;       // steps left wide
    std::int32_t active = 1;
};

enum class Happened : std::uint32_t { hit, broke, paddle, wall, lost, gift, caught, cleared, launched, life, over };
struct Event {
    Happened      what = Happened::hit;
    std::uint32_t id = 0;   // the brick's id, the gift, the paddle
    mgame::V2     at;
    static constexpr bool no_padding = true;
};

struct State {
    World                  world;
    std::array<Paddle, 2>  paddles{};
    std::uint64_t          rng = 1;
    std::int64_t           frame = 0, score = 0;
    std::int32_t           combo = 0, lives = 3, slow = 0, bricks = 0;  // slow: steps left of slow motion (the app's clock reads it)
    double                 speed = 6.5;  // the balls', pixels a step
    std::uint32_t          events = 0, over = 0;  // over: 1 cleared, 2 out of lives
    std::int64_t           gate_from = 0, gate_to = 0;  // the frames the gates stand open: [from, to)
    Rules                  rules;                       // what the gifts do now
    double                 height = field_h;  // the field's: taller than a screen, the camera shows part of it
    double                 zoom = 1;          // the camera (level 3): 1 a screen of the field's bottom, less to see more of it
    std::array<Event, 32>  event{};
};
static_assert(std::is_trivially_copyable_v<State>, "a state is saved and restored as one block");

// The actions: button 0 launches; button 1, held, looks at the whole field
// (a taller one); axis 0 moves (−1 … 1); the pointer, when present, is where
// the paddle goes. `second` is the second paddle's (an agent's, level 5).
inline constexpr int launch_button = 0, look_button = 1;
struct Input {
    mgame::Actions player{}, second{};
    Rules          rules{};        // with `new_rules`: from this step on (a script reloaded, level 6)
    std::uint32_t  new_rules = 0;
};

struct BrickSpec {
    mgame::Box    box;
    Kind          kind = Kind::button;
    std::int32_t  durability = 1;
    std::uint32_t id = 0, row = 0;
    Gift          gift = Gift::none;  // what it always drops (none: by chance)
};
struct Level {
    std::vector<BrickSpec> bricks;
    double                 speed = 6.5;
    bool                   second_paddle = false;  // a paddle at the top too (and no wall there)
    std::int32_t           lives = 3;
    std::uint64_t          seed = 1;
    std::int64_t           gate_from = 0, gate_to = 0;  // its gates open on these frames: [from, to)
    double                 height = field_h;            // taller: the camera zooms out to keep the balls in sight
    Rules                  rules;                       // what its gifts do
};

// The camera's zoom at most out: the whole field's height on a screen.
[[nodiscard]] constexpr double widest(const State& s) noexcept { return field_h / s.height; }

// Whether the gates stand open on the state's frame.
[[nodiscard]] constexpr bool gates_open(const State& s) noexcept { return s.frame >= s.gate_from && s.frame < s.gate_to; }

[[nodiscard]] State start(const Level& level);
void step(State& s, const Input& in);
// The state's bytes, field by field: the same play, the same number everywhere.
[[nodiscard]] std::uint64_t checksum(const State& s) noexcept;

// For mgame::Timeline.
struct Game {
    using State = breakout::State;
    using Input = breakout::Input;
    static void step(State& s, const Input& in) { breakout::step(s, in); }
};

}  // namespace omui::breakout
