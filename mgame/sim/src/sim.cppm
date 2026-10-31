// mgame.sim — the fixed-step simulation and its timeline.
//
//     mgame::Clock clock(120);                        // steps a second
//     for (int n = clock.advance(now); n > 0; --n) timeline.step(input);
//     draw(timeline.state(), clock.alpha());          // between the last step and the next
//
//     timeline.seek(timeline.frame() - 240);          // two seconds back, exactly
//     timeline.step(other);                           // and on from there: a new branch
//
// A game is any type with a `State` and an `Input` of plain data and
// `static void step(State&, const Input&)`: the next frame is a function of
// the frame and the input, so a frame is reached again by replaying the
// inputs after a snapshot, and the result is the same bytes.
export module mgame.sim;

import std;

export namespace mgame {

// The steps due on a clock of its own. `advance(now)` (seconds, any clock)
// answers how many steps to take now: at most `catch_up` (a stall longer
// than that is not owed afterwards); `scale` runs it slower or faster (0.5:
// slow motion); `alpha` is how far toward the next step the clock is, for
// drawing between two states.
class Clock {
public:
    explicit Clock(double steps_per_second = 120, std::int32_t catch_up = 8) noexcept;
    [[nodiscard]] std::int32_t advance(double now) noexcept;
    [[nodiscard]] double alpha() const noexcept { return owed_; }
    [[nodiscard]] double step() const noexcept { return step_; }  // seconds a step lasts
    void reset(double now) noexcept;
    double scale = 1;

private:
    double       step_;
    std::int32_t catch_up_;
    double       last_ = -1;  // the time last seen (−1: none yet)
    double       owed_ = 0;   // steps owed, fractional
};

template <class G>
concept Game = std::is_trivially_copyable_v<typename G::State> && std::is_trivially_copyable_v<typename G::Input> &&
               std::is_default_constructible_v<typename G::Input> &&
               requires(typename G::State& s, const typename G::Input& in) { G::step(s, in); };

// A game's frames: the state now, a snapshot every `every` frames and the
// input of every frame for the last `keep` frames. Any frame in
// [earliest(), frame()] is reached exactly by `seek`; stepping on from one
// forgets the inputs after it (a new branch).
template <Game G>
class Timeline {
public:
    using State = typename G::State;
    using Input = typename G::Input;

    explicit Timeline(const State& initial, std::int64_t keep = 3600, std::int64_t every = 60)
        : every_(std::max<std::int64_t>(1, every)),
          keep_(std::max<std::int64_t>(every_, keep)),
          snapshots_(static_cast<std::size_t>(keep_ / every_ + 2)),
          inputs_(static_cast<std::size_t>(keep_ + every_)),
          state_(initial) {
        snapshot();
    }

    // One frame on: its input recorded, a snapshot when one is due.
    void step(const Input& in) {
        inputs_[slot(frame_)] = in;
        G::step(state_, in);
        ++frame_;
        latest_ = frame_;
        if (frame_ % every_ == 0) snapshot();
    }
    [[nodiscard]] const State& state() const noexcept { return state_; }
    [[nodiscard]] std::int64_t frame() const noexcept { return frame_; }
    // The last frame played on this branch (frame() after a seek back is earlier).
    [[nodiscard]] std::int64_t latest() const noexcept { return latest_; }
    // The earliest frame `seek` reaches: a kept snapshot's.
    [[nodiscard]] std::int64_t earliest() const noexcept {
        const std::int64_t oldest = std::max<std::int64_t>(0, latest_ - keep_);
        return std::max(first_, (oldest + every_ - 1) / every_ * every_);
    }
    // The input played on frame `f` (to frame f + 1), while kept.
    [[nodiscard]] const Input& input(std::int64_t f) const noexcept { return inputs_[slot(f)]; }

    // To frame `f` of this branch: the snapshot at or before it, then its
    // inputs played again. False (nothing changes) outside [earliest(), latest()].
    bool seek(std::int64_t f) {
        if (f < earliest() || f > latest_) return false;
        const std::int64_t from = f / every_ * every_;
        state_ = snapshots_[static_cast<std::size_t>((from / every_) % static_cast<std::int64_t>(snapshots_.size()))];
        for (std::int64_t k = from; k < f; ++k) G::step(state_, inputs_[slot(k)]);
        frame_ = f;
        return true;
    }
    // Stepping from here on is a new branch: what was played after it is forgotten.
    void branch() noexcept { latest_ = frame_; }

private:
    [[nodiscard]] std::size_t slot(std::int64_t f) const noexcept { return static_cast<std::size_t>(f % static_cast<std::int64_t>(inputs_.size())); }
    void snapshot() {
        snapshots_[static_cast<std::size_t>((frame_ / every_) % static_cast<std::int64_t>(snapshots_.size()))] = state_;
        if (frame_ == 0) first_ = 0;
    }

    std::int64_t       every_, keep_;
    std::vector<State> snapshots_;
    std::vector<Input> inputs_;
    State              state_;
    std::int64_t       frame_ = 0, latest_ = 0, first_ = 0;
};

}  // namespace mgame
