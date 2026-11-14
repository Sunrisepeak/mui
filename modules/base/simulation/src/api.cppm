// mlibs.simulation — the fixed-step simulation and its timeline: the stepping
// substrate every simulation runs on (D-133). It holds no solver: physics is
// xengine.dynamics, cellular rules xengine.cellular, a game its own model.
//
//     mlibs::simulation::Clock clock(120);                        // steps a second
//     for (int n = clock.advance(now); n > 0; --n) timeline.step(input);
//     draw(timeline.state(), clock.alpha());          // between the last step and the next
//
//     timeline.seek(timeline.frame() - 240);          // two seconds back, exactly
//     timeline.step(other);                           // and on from there: a new branch
//
// A model has copyable owned State, a plain-data Input and
// `static void step(State&, const Input&)`: no external effects; the next frame is a function of
// the frame and the input, so a frame is reached again by replaying the
// inputs after a snapshot, and the result has the same semantic values.
// State copies must own their mutable storage. Snapshot banks are populated
// from the initial state, so fixed-shape containers reuse capacity thereafter.
export module mlibs.simulation.api;

import std;
import mlibs.trace;
import mlibs.log;

export namespace mlibs::simulation {

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
    double       last_ = 0;
    bool         initialized_ = false;
    double       owed_ = 0;   // steps owed, fractional
};

// A world that is not a value itself — a native engine's, held behind a pointer (Jolt's bodies, Detour's crowd,
// a lattice with its bodies) — made one: a copy is the world's own `branch()` (the whole world, bit for bit), so a
// game's state holds it and a timeline keeps it (a Model's state) with no aliasing between copies. `branch()` gives
// `std::expected` of the world, or of a pointer to it. Empty: made so, or a copy whose branch failed.
template <class World>
    requires requires(const World& world) { world.branch(); }
class Branching {
public:
    Branching() noexcept = default;
    explicit Branching(World world) : world_(std::make_unique<World>(std::move(world))) {}
    explicit Branching(std::unique_ptr<World> world) noexcept : world_(std::move(world)) {}
    Branching(const Branching& other) { copy(other); }
    Branching& operator=(const Branching& other) {
        if (this != &other) copy(other);
        return *this;
    }
    Branching(Branching&&) noexcept = default;
    Branching& operator=(Branching&&) noexcept = default;
    ~Branching() = default;
    [[nodiscard]] explicit operator bool() const noexcept { return world_ != nullptr; }
    [[nodiscard]] World& operator*() const noexcept { return *world_; }
    [[nodiscard]] World* operator->() const noexcept { return world_.get(); }

private:
    void copy(const Branching& other) {
        world_.reset();
        if (!other.world_) return;
        auto branch = other.world_->branch();
        if (!branch) return;
        if constexpr (requires { std::unique_ptr<World>(std::move(*branch)); }) world_ = std::move(*branch);
        else world_ = std::make_unique<World>(std::move(*branch));
    }
    std::unique_ptr<World> world_;
};

template <class G>
concept Model = std::copy_constructible<typename G::State> && std::is_copy_assignable_v<typename G::State> &&
               std::is_trivially_copyable_v<typename G::Input> &&
               std::is_default_constructible_v<typename G::Input> &&
               requires(typename G::State& s, const typename G::Input& in) { G::step(s, in); };

// A model's frames: the state now, a snapshot every `every` frames and the
// input of every frame for the last `keep` frames. Any frame in
// [earliest(), frame()] is reached exactly by `seek`; stepping on from one
// forgets the inputs after it (a new branch).
template <Model G>
class Timeline {
public:
    using State = typename G::State;
    using Input = typename G::Input;

    explicit Timeline(const State& initial, std::int64_t keep = 3600, std::int64_t every = 60)
        : every_(std::max<std::int64_t>(1, every)),
          keep_(std::max<std::int64_t>(every_, keep)),
          snapshots_(static_cast<std::size_t>(keep_ / every_ + 2), initial),
          inputs_(static_cast<std::size_t>(keep_ + every_)),
          state_(initial) {
        snapshot();
        mlibs::log::Log{"simulation"}.debug("history prepared: {} snapshot slots, {} inputs", snapshots_.size(), inputs_.size());
    }

    // One frame on: its input recorded, a snapshot when one is due.
    void step(const Input& in) {
        mlibs::trace::Span span("simulation.step", "simulation");
        // A branch cannot resurrect snapshots/inputs evicted by its old future.
        first_ = earliest();
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
        mlibs::trace::Span span("simulation.seek", "simulation");
        if (f < earliest() || f > latest_) return false;
        const std::int64_t from = f / every_ * every_;
        state_ = snapshots_[static_cast<std::size_t>((from / every_) % static_cast<std::int64_t>(snapshots_.size()))];
        for (std::int64_t k = from; k < f; ++k) G::step(state_, inputs_[slot(k)]);
        frame_ = f;
        return true;
    }
    // Stepping from here on is a new branch: what was played after it is forgotten.
    void branch() noexcept { first_ = earliest(); latest_ = frame_; }

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

// What a simulation promises (D-133 §4, contract 4): four axes, each a
// simulation, a scene or a solver family declares. Authoritative state — what
// a rollback or an agent's replay relies on — runs at `cross_platform` or
// `fixed_point` determinism on `cpu_reference`; a cosmetic effect may take
// any backend and no determinism.
enum class Fidelity : std::uint8_t { cosmetic, plausible, accurate };
enum class Determinism : std::uint8_t {
    none,            // may differ run to run (unordered GPU reductions)
    same_binary,     // the same build on the same machine: the same bits
    cross_platform,  // every platform, compiler and the Web: the same bits
    fixed_point      // integer arithmetic throughout
};
enum class Backend : std::uint8_t { cpu_reference, simd, gpu };
struct Tier {
    Fidelity    fidelity = Fidelity::plausible;
    Determinism determinism = Determinism::cross_platform;
    Backend     backend = Backend::cpu_reference;
    std::int32_t substeps = 1;  // within a step
    friend constexpr bool operator==(const Tier& a, const Tier& b) noexcept {
        return a.fidelity == b.fidelity && a.determinism == b.determinism && a.backend == b.backend && a.substeps == b.substeps;
    }
};
// A tier whose state a rollback or a replay may rely on.
[[nodiscard]] constexpr bool authoritative(const Tier& tier) noexcept {
    return (tier.determinism == Determinism::cross_platform || tier.determinism == Determinism::fixed_point) && tier.backend == Backend::cpu_reference;
}
[[nodiscard]] constexpr std::string_view name(Fidelity f) noexcept {
    switch (f) { case Fidelity::cosmetic: return "cosmetic"; case Fidelity::plausible: return "plausible"; case Fidelity::accurate: return "accurate"; }
    return "?";
}
[[nodiscard]] constexpr std::string_view name(Determinism d) noexcept {
    switch (d) {
        case Determinism::none: return "none";
        case Determinism::same_binary: return "same_binary";
        case Determinism::cross_platform: return "cross_platform";
        case Determinism::fixed_point: return "fixed_point";
    }
    return "?";
}
[[nodiscard]] constexpr std::string_view name(Backend b) noexcept {
    switch (b) { case Backend::cpu_reference: return "cpu_reference"; case Backend::simd: return "simd"; case Backend::gpu: return "gpu"; }
    return "?";
}

}  // namespace mlibs::simulation
