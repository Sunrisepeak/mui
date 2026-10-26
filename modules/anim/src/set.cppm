// openmui.anim:set — the animations of a running UI, evaluated in one pass.
//
// An interface has hundreds of small animations alive at once (a hover fade,
// a list item sliding in, a scroll glide). A `Set` keeps them as parallel
// arrays of scalar channels and evaluates all of them for a frame in one
// loop, without allocating. A vector property uses one channel per lane.
//
// Unlike `Motion`, a channel keeps only its current motion: retargeting
// replaces it (continuing position and velocity). That is what an interactive
// UI needs; video and games, which seek backwards, use `Motion` or timelines.
export module openmui.anim:set;

import std;
import :math;
import :time;
import :ease;
import :motion;

export namespace omui::anim {

struct Policy {
    // The user asked for reduced motion: every channel shows its target.
    bool reduce_motion = false;
};

class Set {
public:
    struct Channel {
        std::uint32_t index      = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t generation = 0;
        // Written out: GCC 16.1 crashes on a defaulted comparison here once an importer uses it.
        friend constexpr bool operator==(const Channel& a, const Channel& b) noexcept { return a.index == b.index && a.generation == b.generation; }
    };

    [[nodiscard]] Channel hold(double value);
    [[nodiscard]] Channel tween(double from, double to, seconds start, seconds duration, Ease e = ease::out_cubic);
    [[nodiscard]] Channel spring(double from, double to, seconds start, SpringParams p = springs::standard,
                                 double velocity = 0.0, Rest rest = {});
    [[nodiscard]] Channel decay(double from, double velocity, seconds start, DecayParams p = {});

    // Retarget from the channel's position and velocity at `now`.
    void spring_to(Channel c, seconds now, double target, std::optional<SpringParams> p = std::nullopt);
    void tween_to(Channel c, seconds now, double target, seconds duration, Ease e = ease::out_cubic);
    void jump(Channel c, double value);

    void remove(Channel c);
    void clear();

    [[nodiscard]] bool valid(Channel c) const noexcept;
    [[nodiscard]] double value(Channel c, seconds t, Policy policy = {}) const noexcept;
    [[nodiscard]] double velocity(Channel c, seconds t) const noexcept;
    [[nodiscard]] double target(Channel c) const noexcept;
    [[nodiscard]] bool settled(Channel c, seconds t) const noexcept;
    // Absolute time at which the channel comes to rest.
    [[nodiscard]] seconds settle_time(Channel c) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return live_; }
    // One past the highest channel index ever handed out: the length `out`
    // must have for `evaluate`.
    [[nodiscard]] std::size_t capacity() const noexcept { return kind_.size(); }

    // out[c.index] = value of c at t, for every live channel; returns how many
    // are still moving (so a host knows whether to schedule another frame).
    friend std::size_t evaluate(const Set& set, seconds t, std::span<double> out, Policy policy);

private:
    enum class Kind : std::uint8_t { free, hold, tween, spring, decay };

    std::uint32_t allocate();
    [[nodiscard]] double value_at(std::uint32_t i, double t, Policy policy) const noexcept;
    [[nodiscard]] double velocity_at(std::uint32_t i, double t) const noexcept;

    std::vector<Kind>          kind_;
    std::vector<std::uint32_t> generation_;
    std::vector<double>        start_;
    std::vector<double>        end_;     // absolute settle time
    std::vector<double>        from_;    // tween start value; hold value
    std::vector<double>        target_;
    std::vector<double>        duration_;
    std::vector<Ease>          ease_;
    std::vector<Oscillator>    osc_;
    std::vector<Glide>         glide_;
    std::vector<SpringParams>  params_;
    std::vector<Rest>          rest_;
    std::vector<std::uint32_t> free_;
    std::size_t                live_ = 0;
};

std::size_t evaluate(const Set& set, seconds t, std::span<double> out, Policy policy = {});

}  // namespace omui::anim
