// gongfu:match — a match between two fighters, one frame at a time (D-85).
//
// tick(p1, p2) plays one frame (1/60 s) from the two pads: the inputs are
// logged, both fighters decide what to do from the state at the frame's start
// (so neither is first), move, are pushed apart and kept on the stage, and
// their active hits, throws and projectiles are found against each other
// before any is applied — two that land on the same frame trade. What
// happened is the frame's events, for what draws, sounds and shakes.
//
// The state is all of it: state() and restore() are a rollback's save and
// load, checksum() its comparison. Nothing reads a clock, a device or a
// float; the same inputs make the same bytes on every machine.
export module gongfu:match;

import std;
import :fixed;
import :input;
import :data;
import :state;

export namespace gongfu {

struct Event {
    enum class Kind : std::uint8_t { hit, counter, block, throw_, tech, projectile_clash, ko, time_over };
    Kind          kind = Kind::hit;
    std::int32_t  attacker = 0;   // 0 or 1
    std::int32_t  move = -1;      // the attacker's move
    std::int32_t  hit = 0;        // the move's hit (or its projectile's)
    std::int32_t  damage = 0;
    std::uint16_t impact = 0;     // what draws and sounds read
    Vec           at;             // where on the stage
};

class Match {
public:
    // Both fighters compiled (they outlive the match); the system checked
    // (check(system) is empty), kept as the match's own.
    Match(SystemDef system, const Fighter& left, const Fighter& right);

    void tick(Pad left, Pad right);

    [[nodiscard]] const State& state() const noexcept { return state_; }
    void restore(const State& s) noexcept { state_ = s; }
    [[nodiscard]] std::uint64_t checksum() const noexcept { return gongfu::checksum(state_); }

    // What happened on the last frame.
    [[nodiscard]] std::span<const Event> events() const noexcept { return events_; }

    [[nodiscard]] const Fighter& fighter(std::int32_t i) const noexcept { return *fighters_[static_cast<std::size_t>(i)]; }
    [[nodiscard]] const SystemDef& system() const noexcept { return system_; }

    // Whether fighter `i` can act on the next frame: a neutral state, a move
    // whose last frame has passed, or a move's frames it may cancel.
    [[nodiscard]] bool actionable(std::int32_t i) const noexcept;
    // Whether it has recovered: a neutral state or a move whose last frame
    // has passed (cancels aside) — what frame advantage is measured by.
    [[nodiscard]] bool recovered(std::int32_t i) const noexcept;

private:
    struct Strike {
        std::int32_t attacker, move, hit;
        bool         projectile;
        std::int32_t slot;  // the projectile's
    };
    void decide(std::int32_t i);
    void queue(std::int32_t i);
    // The cancels open to fighter i now: the moves they allow.
    [[nodiscard]] std::size_t open_cancels(std::int32_t i, std::array<std::int32_t, 32>& out) const noexcept;
    void advance(std::int32_t i);
    void separate();
    void face();
    void resolve();
    void apply(const Strike& s, bool blocked, bool counter);
    [[nodiscard]] Rect hurt(std::int32_t i) const noexcept;
    [[nodiscard]] bool invulnerable(std::int32_t i) const noexcept;
    [[nodiscard]] bool blocking(std::int32_t i, Level level) const noexcept;
    void start(std::int32_t i, std::int32_t move);
    [[nodiscard]] std::int32_t last_active(std::int32_t i) const noexcept;

    SystemDef                                  system_;
    std::array<const Fighter*, 2>              fighters_;
    std::array<std::vector<std::int32_t>, 2>   order_;  // moves by priority, then definition order
    State                                      state_;
    std::vector<Event>                         events_;
};

}  // namespace gongfu
