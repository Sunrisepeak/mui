// The breakout's simulation (S4): a pure function of the state and the
// frame's actions, 120 steps a second.
//
//     omui::breakout::State s = omui::breakout::start(room);    // bricks from the room's layout
//     omui::breakout::step(s, {player, agent});                   // one frame
//     for (std::uint32_t i = 0; i < s.events; ++i) draw(s.event[i]);   // what happened in it
//
// A frame: each paddle moves (the axis, or toward the pointer); a ball on a
// paddle follows it until launched along the aim; each ball curves by its
// spin and moves from one collision to the next (walls, paddles by their
// relative motion, bricks), at most eight a frame, hits at one instant
// ordered by index and their normals combined (a seam between two bricks
// bounces like a face). What a ball meets acts as what it is: a brick loses
// durability and, broken, scores by the chain; a switch flips and moves
// what is on its circuit (shutters open or close, kickers turn, capacitors
// discharge); a capacitor charges and, full, fires a beam along its row; a
// kicker sends the ball where it points; a core, the room's goal, broken
// shuts the room down in a widening wave. The chain since the ball left the
// paddle is banked as sparks when the ball comes back to it; sparks pay for
// rewinds (`Input::pay`). Capsules fall and are caught; a ball out of the
// field is lost, the last one a life. All in one block of plain data: a
// timeline rewinds it, a checksum compares it.
export module openmui.showcase.widget_breakout.sim;

import std;
export import mgame.world;    // its state's types are these packages' (worlds, actions, vectors)
export import mgame.input;
export import mgame.physics;
export import mgame.sim;     // its Game is a timeline's

export namespace omui::breakout {

inline constexpr double field_w = 1280, field_h = 720;  // a screen of the field (a room may be taller: its height)
inline constexpr std::int32_t steps_per_second = 120;
inline constexpr std::int32_t spark_cap = 300;          // the most sparks banked
inline constexpr std::int32_t rewind_cost = 100;        // sparks a rewind costs
inline constexpr std::int32_t rewind_reach = 2 * steps_per_second;  // how far back one goes
inline constexpr std::int32_t duet_full = 6;            // alternate returns that ready the finisher
inline constexpr double       aim_most = 1.2;           // the launch's aim: across per up, ±50°

// What a brick is (its component in the UI), and what a capsule gives.
//   button, card: broken by hits (durability); hidden: the same, unseen (a
//   cache: sparks when broken); wall: stands; gate: a wall but on its
//   frames; shutter: a wall while closed, open by its circuit; toggle: a
//   switch, flipped by every hit, driving its circuit; slider: a capacitor,
//   charged by hits, full it fires; kicker: sends a ball where it points;
//   core: the room's goal.
enum class Kind : std::uint32_t { button, toggle, slider, card, wall, hidden, gate, shutter, kicker, core };
enum class Gift : std::uint32_t { none, wide, multi, slow, pierce, magnet };
[[nodiscard]] constexpr std::int32_t gift_bit(Gift g) noexcept { return 1 << static_cast<std::int32_t>(g); }
inline constexpr std::int32_t classic_gifts = (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4);  // wide, multi, slow, pierce

// Whether hits break a brick of the kind (and it counts toward clearing a room without a core).
[[nodiscard]] constexpr bool breakable(Kind k) noexcept { return k == Kind::button || k == Kind::card || k == Kind::hidden || k == Kind::core; }

struct Body {
    mgame::Box box;
    static constexpr bool no_padding = true;
};
struct Brick {
    Kind          kind = Kind::button;
    std::int32_t  durability = 1;   // a capacitor: the hits that fill it
    std::uint32_t id = 0;           // the UI's brick (its node)
    std::uint32_t row = 0;
    Gift          gift = Gift::none;  // what it always drops, broken (none: now and then, by chance)
    std::uint32_t wire = 0;         // its circuit (0: none): a switch drives it, a shutter, kicker or capacitor is on it
    std::uint32_t on = 0;           // a switch: on; a shutter: open; a kicker: pointing right
    std::int32_t  charge = 0;       // a capacitor: hits stored
    std::int32_t  fuse = 0;         // a capacitor about to fire: steps left (below 0: cooling down, taking no charge)
    static constexpr bool no_padding = true;
};
struct Ball {
    mgame::V2     at, velocity;  // velocity: pixels a step
    double        radius = 8;
    std::uint32_t on = 0;        // on a paddle (its index + 1), waiting to be launched; 0: flying
    std::uint32_t pierce = 0;    // steps left of passing through bricks
    double        spin = 0;      // turning, radians a step (+: clockwise on screen), dying away
    double        dx = 0;        // resting on a paddle: where along it
    std::uint32_t above = 0;     // in a room's upper chamber
    std::uint32_t spare = 0;
    static constexpr bool no_padding = true;
};
struct Capsule {
    mgame::V2     at;
    Gift          gift = Gift::none;
    std::uint32_t spare = 0;
    static constexpr bool no_padding = true;
};

using World = mgame::World<512, Body, Brick, Ball, Capsule>;

// What the gifts do: numbers a room sets, or scripts (the contraptions
// between rooms) — each bounded, all whole (ticks, pixels, percents,
// counts), in the state, so a rewind brings back the rules in force then;
// changed only at a step's start, by the step's input (a replay applies them
// again without any script).
struct Rules {
    std::int32_t wide_steps = 10 * steps_per_second, wide_half = 110;  // a wide paddle: how long, half how wide
    std::int32_t multi_balls = 2;                                        // a flying ball's extra balls (1 … 4: ±20°, then ±40°)
    std::int32_t slow_steps = 8 * steps_per_second, slow_percent = 50;  // slow motion: how long, the clock's speed
    std::int32_t pierce_steps = 5 * steps_per_second;                  // passing through bricks: how long
    std::int32_t drop_one_in = 6;                                        // a broken brick drops a capsule one time in this many
    std::int32_t magnet_catches = 3;                                     // the magnet: returns it holds for another aim
    std::int32_t gift_mask = classic_gifts;                              // what drops by chance (gift_bit of each)
    friend constexpr bool operator==(const Rules& a, const Rules& b) noexcept {  // written out (GCC 16)
        return a.wide_steps == b.wide_steps && a.wide_half == b.wide_half && a.multi_balls == b.multi_balls && a.slow_steps == b.slow_steps &&
               a.slow_percent == b.slow_percent && a.pierce_steps == b.pierce_steps && a.drop_one_in == b.drop_one_in &&
               a.magnet_catches == b.magnet_catches && a.gift_mask == b.gift_mask;
    }
};
// Each within its bounds: wide 1–30 s and 80–300 px half, 1–4 balls, slow 1–20 s at 25–90 %, pierce 1–20 s, one in 2–20, 1–8 catches.
[[nodiscard]] Rules bounded(Rules r) noexcept;

struct Paddle {
    double       x = field_w / 2, y = field_h - 30;
    double       half = 70;      // half its width
    double       moved = 0;      // this step's movement (for the relative sweep, and the spin it gives)
    std::int32_t wide = 0;       // steps left wide
    std::int32_t active = 1;
    std::int32_t magnet = 0;     // returns left that it holds
};

enum class Happened : std::uint32_t {
    hit, broke, paddle, wall, lost, gift, caught, cleared, launched, life, over,
    flipped,     // a switch: `n` its state now
    wired,       // a switch's circuit reached `id` (`at` the switch, `to` it)
    charged,     // a capacitor: `n` its charge
    armed,       // a capacitor full: about to fire
    beam,        // fired from `at` to `to`, `n` strong (`id`: the capacitor; a paddle's finisher: 0xffffffff)
    kicked,      // a kicker sent the ball
    core_hit,    // a core hit, `n` left
    shutdown,    // a core broken (the room's wave starts there)
    breach,      // a ball into the upper chamber (`n` 1: the first time)
    banked,      // a chain of `n` banked as `id` sparks
    chain_lost,  // the last ball lost with a chain of `n` unbanked
    magnet,      // the paddle held the ball
    duet,        // the paddles' alternate returns: `n` of them
    finisher,    // the duet full: the next return fires up its column
    cache,       // a hidden cache broken: sparks
};
struct Event {
    Happened      what = Happened::hit;
    std::uint32_t id = 0;   // the brick's id, the gift, the paddle
    mgame::V2     at, to;
    std::int32_t  n = 0;
    std::uint32_t spare = 0;
    static constexpr bool no_padding = true;
};

struct State {
    World                  world;
    std::array<Paddle, 2>  paddles{};
    std::uint64_t          rng = 1;
    std::int64_t           frame = 0, score = 0;
    std::int32_t           combo = 0, lives = 3, slow = 0, bricks = 0;  // combo: the chain since the ball left the paddle; slow: steps left of slow motion (the app's clock reads it)
    double                 speed = 6.5;  // the balls', pixels a step
    std::uint32_t          events = 0, over = 0;  // over: 1 cleared, 2 out of lives
    std::int64_t           gate_from = 0, gate_to = 0;      // a gate opens once: [from, to) …
    std::int64_t           gate_period = 0, gate_open = 0;  // … or, a period given, for `gate_open` frames of every period from `gate_from`
    Rules                  rules;                           // what the gifts do now
    double                 height = field_h;  // the field's: taller than a screen, the camera shows part of it
    double                 zoom = 1;          // the camera: 1 a screen of the field's bottom, less to see more of it
    double                 chamber = 0;       // a taller room's upper chamber: above this line (0: none)
    double                 aim = 0;           // the launch's: across per up (−aim_most … aim_most)
    std::int32_t           aim_phase = 60, aim_held = 0;  // the aim sweeps while a ball waits, unless held
    std::int32_t           freeze = 0;        // steps of hit-stop left: nothing but the paddles moves
    std::int32_t           ending = 0;        // steps left of the shutdown's wave
    std::int32_t           sparks = 0;        // banked (a debt below 0, after a rewind took back what paid for it)
    std::int32_t           cores = 0;         // cores standing: a room with any is cleared when none is left
    std::int32_t           best = 0;          // the longest chain banked
    std::int32_t           duet = 0, finisher = 0, last_paddle = -1;
    std::int32_t           idle = 0;          // steps since anything moved (the orbit guard)
    std::int32_t           breached = 0;
    mgame::V2              core_at;           // where the last core stood
    std::array<Event, 96>  event{};
};
static_assert(std::is_trivially_copyable_v<State>, "a state is saved and restored as one block");

// The actions: button 0 launches; button 1, held, looks at the whole field
// (a taller one); axis 0 moves (−1 … 1); the pointer, when present, is where
// the paddle goes. `second` is the second paddle's (an agent's).
inline constexpr int launch_button = 0, look_button = 1;
struct Input {
    mgame::Actions player{}, second{};
    Rules          rules{};        // with `new_rules`: from this step on (a script reloaded)
    std::uint32_t  new_rules = 0;
    std::uint32_t  aim_set = 0;    // `aim` holds the launch's aim from this step until the launch
    double         aim = 0;
    std::int32_t   pay = 0;        // sparks spent (a rewind's, on the first step after it)
    std::int32_t   spare = 0;
};

struct BrickSpec {
    mgame::Box    box;
    Kind          kind = Kind::button;
    std::int32_t  durability = 1;
    std::uint32_t id = 0, row = 0;
    Gift          gift = Gift::none;  // what it always drops (none: by chance)
    std::uint32_t wire = 0, on = 0;   // its circuit, and how it starts (a switch on, a shutter open, a kicker right)
};
struct Level {
    std::vector<BrickSpec> bricks;
    double                 speed = 6.5;
    bool                   second_paddle = false;  // a paddle at the top too (and no wall there)
    std::int32_t           lives = 3;
    std::uint64_t          seed = 1;
    std::int64_t           gate_from = 0, gate_to = 0;      // its gates open on these frames: [from, to) …
    std::int64_t           gate_period = 0, gate_open = 0;  // … or every period from gate_from, for gate_open frames
    double                 height = field_h;            // taller: the camera zooms out to keep the balls in sight
    double                 chamber = 0;                 // an upper chamber above this line
    Rules                  rules;                       // what its gifts do
    std::int64_t           score = 0;                   // carried from the rooms before
    std::int32_t           sparks = 0;
    std::int32_t           aim_phase = 60;              // where the aim's sweep starts (60: straight up, rising to the right)
    Gift                   opening = Gift::none;        // a capsule falling onto the paddle as it begins (a contraption just chosen)
};

// The camera's zoom at most out: the whole field's height on a screen.
[[nodiscard]] constexpr double widest(const State& s) noexcept { return field_h / s.height; }

// Whether the gates stand open on the state's frame.
[[nodiscard]] constexpr bool gates_open(const State& s) noexcept {
    if (s.gate_period > 0) return s.frame >= s.gate_from && (s.frame - s.gate_from) % s.gate_period < s.gate_open;
    return s.frame >= s.gate_from && s.frame < s.gate_to;
}

// Whether a brick stops a ball (and a beam) now: walls, closed shutters, closed gates.
[[nodiscard]] constexpr bool blocks(const State& s, const Brick& b) noexcept {
    return b.kind == Kind::wall || (b.kind == Kind::shutter && b.on == 0) || (b.kind == Kind::gate && !gates_open(s));
}

// The direction a launch takes now (unit, up the field).
[[nodiscard]] mgame::V2 aim_direction(const State& s) noexcept;
// Where a kicker sends a ball (unit).
[[nodiscard]] mgame::V2 kick_direction(const Brick& b) noexcept;

[[nodiscard]] State start(const Level& level);
void step(State& s, const Input& in);
// The state's bytes, field by field: the same play, the same number everywhere.
[[nodiscard]] std::uint64_t checksum(const State& s) noexcept;

// The path a ball of `radius` would take from `at` along `velocity` among the
// walls and the bricks as they stand (not the paddles, nothing changing):
// where it starts and each place it turns, up to `points`' size, over
// `length` pixels of travel or until it leaves the field. How many were written.
std::size_t trace(const State& s, mgame::V2 at, mgame::V2 velocity, double radius, std::span<mgame::V2> points, double length = 1600);

// For mgame::Timeline.
struct Game {
    using State = breakout::State;
    using Input = breakout::Input;
    static void step(State& s, const Input& in) { breakout::step(s, in); }
};

}  // namespace omui::breakout
