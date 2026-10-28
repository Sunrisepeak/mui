// gongfu:data — characters and rules as data (D-85).
//
// A FighterDef is a character: its body (health, walking, jumping, the boxes
// it is hit on and pushes with) and its moves. A MoveDef is one move as a
// frame-by-frame description — how it is asked for, from where (standing,
// crouching, in the air), when its hits are active and what each does, when
// it can be cancelled and into what, when it is invulnerable, how it moves —
// and nothing else: the simulation reads its frame data, it does not run
// code. A SystemDef is the game's rules: combo scaling, counter hits, throw
// teching, the round, the stage.
//
// These are what a data file (`*.fighter.gongfu.toml`) becomes and what a
// community designer will write; `compile` checks a definition — every
// problem, named with the move and the field — and resolves it into the
// tables the simulation indexes.
export module gongfu:data;

import std;
import :fixed;
import :input;

export namespace gongfu {

// Where a move can start, and how a fighter stands while it is not moving.
enum class Stance : std::uint8_t { standing, crouching, air };

// What blocks a hit: mid by blocking standing or crouching, low only
// crouching, overhead only standing.
enum class Level : std::uint8_t { mid, low, overhead };

// A range of a move's frames, the first frame 1, both ends included.
struct Frames {
    std::int32_t from = 0, to = 0;
    [[nodiscard]] constexpr bool contains(std::int32_t f) const noexcept { return f >= from && f <= to; }
    [[nodiscard]] constexpr std::int32_t length() const noexcept { return to - from + 1; }
};

// One hit of a move: active on its frames, with its boxes.
struct HitDef {
    Frames             active;
    std::vector<Box>   boxes;
    std::int32_t       damage = 0;
    std::int32_t       chip = 0;        // damage through a block
    std::int32_t       hitstun = 0;     // frames
    std::int32_t       blockstun = 0;   // frames
    std::int32_t       hitstop = 0;     // frames the defender stands still (and the attacker, unless hitstop_attacker says)
    Fixed              pushback;        // per frame of stun, against the defender
    Level              level = Level::mid;
    bool               knockdown = false;
    Vec                launch;          // an airborne defender's new velocity (forward, up)
    std::uint16_t      impact = 0;      // its impact profile: an index into FighterDef::impacts (D-85 §8.4)
    std::int32_t       hitstop_attacker = -1;  // the attacker's own stop (−1: as `hitstop`); a projectile's owner never stops
    std::int32_t       down = 0;        // a knockdown's frames on the ground (0: the system's)
    std::int32_t       juggle = 0;      // a launch: the juggle points the defender is given
    std::int32_t       juggle_cost = 1; // on a juggled defender: the points this hit uses (too few left: an air reset)
};

// What invulnerability is to: a set of these bits.
inline constexpr std::uint8_t to_strike = 1, to_air = 2, to_projectile = 4, to_throw = 8, to_all = 15;

// A move's frames something passes through: strikes from the ground, from
// the air, projectiles, throws (all of them unless said).
struct Invulnerable {
    Frames       frames;
    std::uint8_t to = to_all;
};

// A move's own hurtboxes on some of its frames (an arm stretched out can be
// hit): in place of the standing or crouching box. gongfu.rules bakes them
// from the animation.
struct HurtDef {
    Frames           frames;
    std::vector<Box> boxes;
};

// A move can be cancelled into others on these frames — on hit or block, or always.
struct CancelDef {
    Frames                   frames;
    std::vector<std::string> into;
    bool                     on_contact = true;  // only after a hit or a block of this move
};

struct ThrowDef {
    Frames       active;
    Box          range;
    std::int32_t damage = 0;
    std::int32_t tech_window = 7;  // frames the defender has to press throw back, from the frame it takes hold
    bool         swap = false;     // a back throw: the two change places as it takes hold
};

struct ProjectileDef {
    std::int32_t spawn = 0;     // the move's frame it appears on
    Vec          offset;        // from the fighter, forward and up
    Vec          velocity;      // forward and up, per frame (one coming down is gone where it meets the ground)
    std::int32_t life = 120;    // frames
    HitDef       hit;           // its active frames count from its spawn
    std::int32_t hits = 1;      // how many times it hits before it is gone
    std::int32_t interval = 0;  // frames after a hit before it can hit again
    std::int32_t strength = 1;  // meeting another: the weaker is gone, the stronger goes on weakened by it; equal, both are gone
    std::int32_t limit = 0;     // at most this many of its owner's limited projectiles at once (0: not limited); the move is not started past it
};

// A step of a move's own movement: from this frame on, this velocity
// (forward, up), until the next step or the move's end.
struct StepDef {
    std::int32_t frame = 1;
    Vec          velocity;
};

struct MoveDef {
    std::string                  name;
    Command                      command;
    Stance                       stance = Stance::standing;
    std::int32_t                 total = 0;         // frames, startup to the last frame of recovery
    std::int32_t                 priority = 0;      // the higher wins when two are asked for at once (equal: the more particular command)
    std::vector<HitDef>          hits;
    std::vector<CancelDef>       cancels;
    std::vector<Invulnerable>    invulnerable;
    std::vector<HurtDef>         hurt;              // its own hurtboxes on some frames
    std::vector<StepDef>         steps;
    std::optional<ThrowDef>      throw_;
    std::optional<ProjectileDef> projectile;
    std::int32_t                 meter_cost = 0;
    std::int32_t                 meter_gain = 0;    // on hit; half on block
    std::int32_t                 freeze = 0;        // frames both fighters stand still at its first frame (a super's flash)
};

struct FighterDef {
    std::string          name;
    std::int32_t         health = 10000;
    Fixed                walk_forward, walk_back;  // per frame
    std::int32_t         prejump = 4;              // frames before leaving the ground
    Fixed                jump_up;                  // the jump's initial upward speed
    Fixed                jump_forward;             // a forward jump's speed along the stage
    Fixed                gravity;                  // per frame
    std::int32_t         landing = 3;              // frames after landing from a jump
    Box                  push;                     // standing and crouching: what keeps two fighters apart
    Box                  hurt_standing, hurt_crouching, hurt_air;
    std::vector<MoveDef> moves;
    // The impact profiles its hits name, by HitDef::impact: what a picture
    // draws for a hit (a look's `impact.<name>`); the simulation never reads them.
    std::vector<std::string> impacts;
};

struct SystemDef {
    // Damage of the n-th hit of a combo, in percent (the last repeats).
    std::vector<std::int32_t> scaling{100, 100, 80, 70, 60, 50, 40, 30};
    std::int32_t counter_damage = 120;  // percent, a hit during the defender's startup
    std::int32_t counter_hitstun = 2;   // extra frames
    std::int32_t round_frames = 99 * 60;
    Fixed        stage_width = 1920_fx;
    Fixed        start_distance = 400_fx;
    std::int32_t meter_max = 1000;
    std::int32_t knockdown = 30;  // frames down
    std::int32_t wakeup = 10;     // frames getting up, invulnerable
    // Holding back near an opponent's move — starting or active — guards in
    // place instead of walking away (proximity guard).
    Fixed        proximity_guard = 300_fx;
    Socd         socd = Socd::up_wins;
};

struct Problem {
    std::string where;  // "Shoto/236P/hits[0].active"
    std::string text;
};

// A fighter checked and resolved: its moves' cancels by index.
struct Fighter {
    FighterDef                                          def;
    std::vector<std::vector<std::vector<std::int32_t>>> cancel_targets;  // per move, per cancel: the moves it allows
    [[nodiscard]] std::int32_t find(std::string_view move) const noexcept;
    // The first frame any of a move's hits (or its throw, or its projectile) is active.
    [[nodiscard]] std::int32_t startup(std::int32_t move) const noexcept;
};

[[nodiscard]] std::expected<Fighter, std::vector<Problem>> compile(FighterDef def);
[[nodiscard]] std::vector<Problem> check(const SystemDef& system);

}  // namespace gongfu
