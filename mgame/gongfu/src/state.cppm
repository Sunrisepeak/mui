// gongfu:state — everything a match is at one frame, as one block of plain
// data (D-85). A rollback saves it by a copy and restores it by a copy; its
// hash is the hash of its bytes, which have no padding (checked at compile
// time), so two machines that played the same inputs hold the same bytes.
export module gongfu:state;

import std;
import :fixed;
import :input;

export namespace gongfu {

// What a fighter is doing.
enum class Mode : std::int32_t {
    idle,
    walk_forward,
    walk_back,
    crouch,
    prejump,
    air,        // jumping, falling
    landing,
    move,       // a move of its definition (`move`, `move_frame`)
    hitstun,    // on the ground or, launched, in the air
    blockstun,
    knockdown,
    wakeup,
    thrown,
    ko,
    reset,      // an air reset: falling untouchable, landing free
    clash,      // the recovery after a clash (`stun` frames)
    broken,     // a guard break: staggered, unable to block or act (`stun` frames)
    cinematic,  // locked in a level-3 super's cinematic (State::lock)
};
// How many modes there are: what a table by mode is sized by.
inline constexpr std::size_t mode_count = static_cast<std::size_t>(Mode::cinematic) + 1;

struct FighterState {
    Vec          pos, vel;
    std::int32_t facing = 1;         // +1 right, −1 left
    std::int32_t health = 0;
    std::int32_t meter = 0;
    Mode         mode = Mode::idle;
    std::int32_t stun = 0;           // frames left: prejump, landing, hitstun, blockstun, knockdown, wakeup
    std::int32_t move = -1;          // the move's index, in mode `move`
    std::int32_t move_frame = 0;     // its current frame, from 1
    std::int32_t landed = 0;         // which of the move's hits have connected (bits)
    std::int32_t contact = 0;        // whether the move hit or was blocked (cancels on contact)
    std::int32_t combo = 0;          // hits taken in the combo this fighter is in
    std::int32_t jump = 0;           // a jump's direction along the stage: −1, 0, +1 (relative)
    Fixed        push;               // pushed this much along the stage per frame of stun
    std::int32_t tech = 0;           // frames left to break the throw it is in
    std::int32_t thrower = 0;        // 1 when it is the fighter throwing (mode move) and the throw connected
    std::int32_t throw_damage = 0;   // what the throw it is in does when it is not broken
    std::int32_t queued = -1;        // a move asked for while standing still (a hitstop, a super's flash): it starts when that ends
    std::int32_t stop = 0;           // frames of hitstop left: it stands still
    std::int32_t juggle = 0;         // juggle points left while launched
    std::int32_t relaunched = 0;     // 1 once launched again in this juggle
    std::int32_t protect = 0;        // it cannot be thrown while more than 0: 3 frames after waking up, 2 after hitstun or blockstun (counted down at each frame's start)
    std::int32_t posture = 0;        // posture taken (system 0.3): at the fighter's max, a guard break
    std::int32_t quiet = 0;          // frames since it last blocked, lost a clash or was hit: posture recovers after a while
    std::int32_t steady = 0;         // frames of protection after a guard break (no posture taken), counted while able to act
    std::int32_t deflected = 0;      // frames left in which a fresh back press deflects the next hit of a string
    std::int32_t scale_cap = 100;    // its combo's damage scaling at most this (percent): 80 for a combo from a guard break
    std::int32_t super_scale = 0;    // the scaling its super locked at its first hit (percent; 0: none yet)
    std::int32_t clash_cancel = 0;   // 1 while a perfect clash's recovery may be cancelled into a special
    std::int32_t armored = 0;        // hits its move's armour has taken
    InputLog     log;
};

struct ProjectileState {
    std::int32_t alive = 0;
    std::int32_t owner = 0;    // 0 or 1: whose it is now (a reflected one changes side)
    std::int32_t from = 0;     // whose move defines it
    std::int32_t move = -1;    // the move that spawned it
    std::int32_t frame = 0;    // frames since it spawned, from 1
    std::int32_t facing = 1;
    Vec          pos;
    std::int32_t hits = 0;     // hits left
    std::int32_t strength = 0;
    std::int32_t stop = 0;     // frames it stands still after a hit
    std::int32_t wait = 0;     // frames before it can hit again
    std::int32_t reflected = 0;
};

// A set piece standing (G-15): whose, where, how much more it takes, how
// long it stands, how many projectiles it still turns back, what it
// shatters into; which fighters' strikes touched it last frame (bit per
// fighter: a strike costs it once, however many frames it stays in it);
// when it was placed (the oldest gives way).
struct PieceState {
    std::int32_t alive = 0;
    std::int32_t owner = 0;
    Vec          pos;
    std::int32_t durability = 0;
    std::int32_t life = 0;
    std::int32_t reflects = 0;
    std::int32_t shard = -1;   // the owner's move whose projectile its shards are
    std::int32_t shards = 0;
    std::int32_t touched = 0;
    std::int32_t placed = 0;   // the frame
};

// A level-3 super's cinematic under way: who, which move, its frame, and
// where and which way the attacker stood when it began.
struct Lock {
    std::int32_t owner = -1;  // the attacker (−1: none)
    std::int32_t move = -1;
    std::int32_t frame = 0;   // frames of the lock played, from 1
    std::int32_t facing = 1;
    Fixed        x, y;
};

struct State {
    static constexpr std::size_t projectiles = 16;
    static constexpr std::size_t piece_slots = 6;  // three each
    std::int32_t                             frame = 0;    // frames played
    std::int32_t                             timer = 0;    // frames left in the round
    std::int32_t                             freeze = 0;   // frames of a super's flash
    std::int32_t                             over = 0;     // the round has ended
    std::int32_t                             winner = -1;  // 0, 1, or 2 for a draw
    std::array<FighterState, 2>              f{};
    std::array<ProjectileState, projectiles> p{};
    std::array<PieceState, piece_slots>      pieces{};
    Lock                                     lock{};
};

static_assert(std::is_trivially_copyable_v<State>);
static_assert(std::has_unique_object_representations_v<State>, "a State's bytes are its value: no padding, so its hash is the same everywhere");

// FNV-1a of the state's bytes.
[[nodiscard]] std::uint64_t checksum(const State& s) noexcept;

}  // namespace gongfu
