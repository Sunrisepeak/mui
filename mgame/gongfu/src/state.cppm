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
};

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
    std::int32_t queued = -1;        // a cancel asked for during a hitstop: the move it starts when the hitstop ends
    InputLog     log;
};

struct ProjectileState {
    std::int32_t alive = 0;
    std::int32_t owner = 0;    // 0 or 1
    std::int32_t move = -1;    // the move that spawned it
    std::int32_t frame = 0;    // frames since it spawned, from 1
    std::int32_t facing = 1;
    Vec          pos;
};

struct State {
    static constexpr std::size_t projectiles = 8;
    std::int32_t                             frame = 0;    // frames played
    std::int32_t                             timer = 0;    // frames left in the round
    std::int32_t                             hitstop = 0;  // frames both fighters stand frozen
    std::int32_t                             freeze = 0;   // frames of a super's flash
    std::int32_t                             over = 0;     // the round has ended
    std::int32_t                             winner = -1;  // 0, 1, or 2 for a draw
    std::array<FighterState, 2>              f{};
    std::array<ProjectileState, projectiles> p{};
};

static_assert(std::is_trivially_copyable_v<State>);
static_assert(std::has_unique_object_representations_v<State>, "a State's bytes are its value: no padding, so its hash is the same everywhere");

// FNV-1a of the state's bytes.
[[nodiscard]] std::uint64_t checksum(const State& s) noexcept;

}  // namespace gongfu
