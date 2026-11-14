// gongfu.ai — a computer opponent, and fighters measured against each other.
//
// A bot reads what a fighter's moves are for from their data (the kit), sees
// the other side `reaction` frames late, and chooses as a person might:
// guard a strike seen coming, answer a jump, punish a whiff, confirm a hit
// into a special, and by distance zone, poke, walk in, jump in, throw and
// spend meter. Every choice comes from its own seeded generator, so a round
// between two bots plays the same every time, on every machine. `versus`
// plays a pair many rounds with the sides swapped every other round: the
// report the roster's balance is tuned by (characters 2 design §7).
export module gongfu.ai;

import std;
import gongfu;

export namespace gongfu::ai {

// How a bot plays.
struct Style {
    std::int32_t reaction = 14;    // frames before it sees what the other does (a person's ~230 ms)
    std::int32_t aggression = 55;  // percent: at a distance, coming in rather than waiting
    std::int32_t guard = 70;       // percent: guards a strike it saw coming
    std::int32_t anti_air = 60;    // percent: answers a jump it saw
    std::int32_t punish = 70;      // percent: punishes a whiff in reach
    std::int32_t offense = 100;    // percent: of its own turns, how many it attacks in (0: it only answers)
    std::int32_t jump_over = 8;    // percent a frame: jumps a projectile coming from far enough (each frame it is in range)
};

// A fighter's moves by what they are for, read from their data (indices
// into its moves; −1 or empty when it has none).
struct Kit {
    struct Poke {
        std::int32_t move = -1;
        std::int32_t reach = 0;    // how far in front its first hit reaches
        std::int32_t startup = 0;
        bool         low = false;  // crouching
    };
    std::vector<Poke>         pokes;        // ground normals, nearest first
    std::vector<std::int32_t> jump_ins;     // air normals
    std::vector<std::int32_t> specials;     // ground specials that strike
    std::vector<std::int32_t> projectiles;  // that throw something
    std::vector<std::int32_t> setups;       // that place, mark or call (panes, a mark): no hit of their own
    std::int32_t              anti_air = -1;   // invulnerable to the air from its first frames
    std::int32_t              throw_ = -1;     // the plain throw
    std::int32_t              command_throw = -1;
    std::int32_t              air_throw = -1;
    std::int32_t              dash = -1, backdash = -1;
    std::array<std::vector<std::int32_t>, 4> supers{};  // by level (1–3)
    std::int32_t              throw_reach = 0, command_reach = 0;
};
[[nodiscard]] Kit read_kit(const Fighter& fighter);

// The pads that ask for a move, facing `facing` (screen directions): a
// motion's steps, a charge held, a rotation's laps, then the press.
[[nodiscard]] std::vector<Pad> pads_for(const Command& command, std::int32_t facing);

class Bot {
public:
    // Plays side `side` (0 left, 1 right) of a match of this fighter.
    Bot(const Fighter& self, std::int32_t side, std::uint64_t seed, Style style = {});

    // The pad for the match's next frame. Called once a frame, in order.
    [[nodiscard]] Pad next(const Match& match);

private:
    struct Seen {
        Mode         mode = Mode::idle;
        std::int32_t move = -1, move_frame = 0;
        Fixed        x, y, vy;
    };
    [[nodiscard]] std::uint32_t roll() noexcept;
    [[nodiscard]] bool chance(std::int32_t percent) noexcept { return static_cast<std::int32_t>(roll() % 100) < percent; }
    void plan_move(std::int32_t move, std::int32_t facing);
    void plan_hold(int numpad, std::int32_t frames, std::int32_t facing);
    [[nodiscard]] Pad guard_pad(const Match& match, const Seen& other) const;

    const Fighter*     self_;
    std::int32_t       side_;
    std::uint64_t      rng_;
    Style              style_;
    Kit                kit_;
    std::vector<Pad>   plan_;
    std::size_t        at_ = 0;
    bool               loose_ = false;  // the plan is a hold or a walk: what it sees may cut it short
    std::array<Seen, 64> seen_{};  // the other side, by frame (frame & 63)
    std::int32_t       guarded_move_ = -2;  // the other's move it decided on (guard or not) last
    bool               guarding_ = false;
};

// One round between two bots.
struct Round {
    std::int32_t winner = 2;  // 0, 1, or 2 for a draw
    std::int32_t frames = 0;
    std::array<std::int32_t, 2> damage{};  // dealt by each side
    std::array<std::int32_t, 2> supers{};  // started by each side
    std::array<std::vector<std::int64_t>, 2> by_move{};  // each side's damage dealt, by its move
};
[[nodiscard]] Round play(const SystemDef& system, const Fighter& left, const Fighter& right, std::uint64_t seed, Style style = {});

// A pair's record: `rounds` rounds, `a` on the left in the even ones.
struct Record {
    std::int32_t rounds = 0, draws = 0;
    std::array<std::int32_t, 2> wins{};    // a's, b's
    std::array<std::int64_t, 2> damage{};  // dealt, summed
    std::array<std::int32_t, 2> supers{};
    std::array<std::vector<std::int64_t>, 2> by_move{};  // a's and b's damage, by move, summed
    std::int64_t frames = 0;               // summed
    [[nodiscard]] double win_rate(std::size_t who) const noexcept {
        return rounds == 0 ? 0.0 : (wins[who] + 0.5 * draws) / static_cast<double>(rounds);
    }
};
[[nodiscard]] Record versus(const SystemDef& system, const Fighter& a, const Fighter& b, std::int32_t rounds, std::uint64_t seed, Style style = {});

}  // namespace gongfu::ai
