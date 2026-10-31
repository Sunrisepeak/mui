// hanim:motion — the animation primitives, every one a function of time.
//
//   Tween<T>     from → to over a duration, through an ease
//   Track<T>     keyframes, each segment with its own ease, holds and cuts
//   Spring<T>    a damped harmonic oscillator, solved in closed form
//   Decay<T>     inertial glide (a fling), solved in closed form
//   Motion<T>    an interruptible history of the above: retarget a spring in
//                mid-flight and the velocity carries over; seek to any time,
//                earlier or later, and get the same value as playing to it
//
// Nothing here integrates step by step. A value at time t is computed from t
// and the parameters alone, which is what makes a video frame renderable in
// any order, a game rewindable, and a test reproducible bit for bit.
export module hanim:motion;

import std;
import :math;
import :time;
import :ease;
import :value;

export namespace hanim {

// ── tween ──────────────────────────────────────────────────────────────────

template <Animatable T>
struct Tween {
    T       from{};
    T       to{};
    seconds duration{0.3};
    Ease    ease = ease::out_cubic;
    seconds delay{0};

    [[nodiscard]] seconds total() const noexcept { return delay + duration; }

    [[nodiscard]] T value(seconds t) const {
        double local = (t - delay).count();
        if (local <= 0.0) return from;
        double d = duration.count();
        if (d <= 0.0 || local >= d) return to;
        return lerp(from, to, ease(local / d));
    }

    [[nodiscard]] lanes_of<T> velocity(seconds t) const {
        lanes_of<T> v{};
        double local = (t - delay).count();
        double d = duration.count();
        if (local < 0.0 || d <= 0.0 || local > d) return v;
        auto a = lanes<T>::to(from);
        auto b = lanes<T>::to(to);
        double k = ease.slope(local / d) / d;
        for (std::size_t i = 0; i < v.size(); ++i) v[i] = (b[i] - a[i]) * k;
        return v;
    }
};

// ── keyframes ──────────────────────────────────────────────────────────────

template <Animatable T>
struct Key {
    seconds time{0};
    T       value{};
    Ease    ease = ease::linear;  // shapes the segment that starts at this key
    bool    hold = false;         // keep this value until the next key, then cut
};

template <Animatable T>
class Track {
public:
    // Adds a key; keys stay sorted by time. Two keys at the same time make a
    // cut: before it the first value, from it on the second.
    Track& key(seconds t, T value, Ease to_next = ease::linear) {
        insert({t, std::move(value), to_next, false});
        return *this;
    }

    Track& hold(seconds t, T value) {
        insert({t, std::move(value), ease::linear, true});
        return *this;
    }

    [[nodiscard]] bool empty() const noexcept { return keys_.empty(); }
    [[nodiscard]] std::span<const Key<T>> keys() const noexcept { return keys_; }
    [[nodiscard]] seconds start() const noexcept { return keys_.empty() ? seconds{0} : keys_.front().time; }
    [[nodiscard]] seconds end() const noexcept { return keys_.empty() ? seconds{0} : keys_.back().time; }

    // O(log n). Before the first key: the first value; after the last: the last.
    [[nodiscard]] T value(seconds t) const {
        if (keys_.empty()) return T{};
        auto it = std::ranges::upper_bound(keys_, t, {}, &Key<T>::time);
        if (it == keys_.begin()) return keys_.front().value;
        if (it == keys_.end()) return keys_.back().value;
        const Key<T>& a = *(it - 1);
        const Key<T>& b = *it;
        if (a.hold) return a.value;
        double span = (b.time - a.time).count();
        if (span <= 0.0) return b.value;
        return lerp(a.value, b.value, a.ease((t - a.time).count() / span));
    }

private:
    void insert(Key<T> k) {
        auto it = std::ranges::upper_bound(keys_, k.time, {}, &Key<T>::time);
        keys_.insert(it, std::move(k));
    }
    std::vector<Key<T>> keys_;
};

// ── spring ─────────────────────────────────────────────────────────────────

struct SpringParams {
    double stiffness = 170.0;
    double damping   = 26.0;
    double mass      = 1.0;

    // The designer's form (as SwiftUI states it): `response` is the period of
    // the undamped oscillation in seconds, `bounce` 0 is critically damped,
    // toward 1 bouncier, below 0 overdamped.
    [[nodiscard]] static SpringParams response(double response_seconds, double bounce = 0.0) noexcept;

    [[nodiscard]] double natural_frequency() const noexcept;  // ω0 = √(k/m)
    [[nodiscard]] double damping_ratio() const noexcept;      // ζ = c / (2√(km))

    // Written out: GCC 16.1 crashes on a defaulted comparison here once an importer uses it.
    friend constexpr bool operator==(const SpringParams& a, const SpringParams& b) noexcept { return a.stiffness == b.stiffness && a.damping == b.damping && a.mass == b.mass; }
};

namespace springs {
inline constexpr SpringParams standard{170, 26, 1};
inline constexpr SpringParams gentle{120, 14, 1};
inline constexpr SpringParams wobbly{180, 12, 1};
inline constexpr SpringParams stiff{210, 20, 1};
inline constexpr SpringParams slow{280, 60, 1};
inline constexpr SpringParams molasses{280, 120, 1};
}  // namespace springs

// When a motion counts as finished: every lane within `delta` of its target
// and slower than `speed` (units per second). From that instant on it is
// exactly at the target, so "settled" is a moment, not an approximation.
struct Rest {
    double delta = 1e-3;
    double speed = 1e-3;
};

// One lane of a spring, from position x0 and velocity v0 at local time 0.
class Oscillator {
public:
    Oscillator() = default;
    Oscillator(double x0, double v0, double target, SpringParams p, Rest rest = {}) noexcept;

    [[nodiscard]] double position(double t) const noexcept;
    [[nodiscard]] double velocity(double t) const noexcept;
    [[nodiscard]] double settle_time() const noexcept { return settle_; }
    [[nodiscard]] double target() const noexcept { return target_; }
    [[nodiscard]] double start() const noexcept { return x0_; }
    [[nodiscard]] double start_velocity() const noexcept { return v0_; }

private:
    enum class Regime : std::uint8_t { rest, under, critical, over };
    Regime regime_ = Regime::rest;
    double x0_ = 0, v0_ = 0, target_ = 0;
    double a_ = 0, b_ = 0;      // under / critical coefficients; over: c1, c2
    double k_ = 0;              // under: ζω0, critical: ω0, over: r1
    double w_ = 0;              // under: ωd,  over: r2
    double settle_ = 0;
};

template <Animatable T>
struct Spring {
    T            from{};
    T            to{};
    lanes_of<T>  initial_velocity{};
    SpringParams params{};
    Rest         rest{};
    seconds      start{0};

    [[nodiscard]] T value(seconds t) const {
        if (t <= start) return from;
        auto a = lanes<T>::to(from);
        auto b = lanes<T>::to(to);
        lanes_of<T> out{};
        bool all_settled = true;
        double local = (t - start).count();
        for (std::size_t i = 0; i < out.size(); ++i) {
            Oscillator o(a[i], initial_velocity[i], b[i], params, rest);
            out[i] = o.position(local);
            all_settled = all_settled && local >= o.settle_time();
        }
        return all_settled ? to : lanes<T>::from(out);
    }

    [[nodiscard]] seconds settle_time() const {
        auto a = lanes<T>::to(from);
        auto b = lanes<T>::to(to);
        double s = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            s = std::max(s, Oscillator(a[i], initial_velocity[i], b[i], params, rest).settle_time());
        return start + seconds(s);
    }
};

// ── decay ──────────────────────────────────────────────────────────────────

// `rate` is the fraction of velocity kept per millisecond (0.998 is the
// platform scroll view's normal deceleration, 0.99 a fast stop).
struct DecayParams {
    double rate = 0.998;
    Rest   rest{.delta = 0.5, .speed = 5.0};
};

// One lane of an inertial glide: x(t) = x0 + v0·τ·(1 − e^(−t/τ)).
class Glide {
public:
    Glide() = default;
    Glide(double x0, double v0, DecayParams p) noexcept;
    [[nodiscard]] double position(double t) const noexcept;
    [[nodiscard]] double velocity(double t) const noexcept;
    [[nodiscard]] double settle_time() const noexcept { return settle_; }
    [[nodiscard]] double destination() const noexcept { return x0_ + v0_ * tau_; }

private:
    double x0_ = 0, v0_ = 0, tau_ = 0, settle_ = 0;
};

template <Animatable T>
struct Decay {
    T           from{};
    lanes_of<T> velocity{};
    DecayParams params{};
    seconds     start{0};

    [[nodiscard]] T value(seconds t) const {
        if (t <= start) return from;
        auto a = lanes<T>::to(from);
        lanes_of<T> out{};
        double local = (t - start).count();
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = Glide(a[i], velocity[i], params).position(local);
        return lanes<T>::from(out);
    }

    [[nodiscard]] T destination() const {
        auto a = lanes<T>::to(from);
        lanes_of<T> out{};
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = Glide(a[i], velocity[i], params).destination();
        return lanes<T>::from(out);
    }
};

// ── interruptible, seekable motion ─────────────────────────────────────────

// A value that is moved by a sequence of motions, each starting where the
// previous one was at the moment it took over (position and velocity), so an
// interrupted animation never jumps and never loses speed. The sequence is the
// history: sampling at any time finds the motion that was active then.
//
// Starting a motion at a time earlier than the last one discards the motions
// after it: that is a branch of history, the game's "rewind, then play on".
template <Animatable T>
class Motion {
public:
    static constexpr int N = lanes<T>::count;

    explicit Motion(T initial = T{}) : initial_(lanes<T>::to(initial)) {}

    // A spring toward `target` from time t.
    Motion& spring_to(seconds t, const T& target, SpringParams p = springs::standard, Rest rest = {}) {
        Segment s = begin(t, Kind::spring);
        auto b = lanes<T>::to(target);
        double settle = 0;
        for (int i = 0; i < N; ++i) {
            s.osc[i] = Oscillator(s.x0[i], s.v0[i], b[i], p, rest);
            settle = std::max(settle, s.osc[i].settle_time());
        }
        s.target = b;
        s.end = t.count() + settle;
        push(s);
        return *this;
    }

    // A tween toward `target` from time t; it starts from wherever the value
    // is at t (not from a remembered "from").
    Motion& tween_to(seconds t, const T& target, seconds duration, Ease e = ease::out_cubic) {
        Segment s = begin(t, Kind::tween);
        s.target = lanes<T>::to(target);
        s.duration = duration.count();
        s.ease = e;
        s.end = t.count() + std::max(0.0, s.duration);
        push(s);
        return *this;
    }

    // A glide with the velocity it has at t plus `impulse`.
    Motion& decay(seconds t, const lanes_of<T>& impulse = {}, DecayParams p = {}) {
        Segment s = begin(t, Kind::decay);
        double settle = 0;
        for (int i = 0; i < N; ++i) {
            s.glide[i] = Glide(s.x0[i], s.v0[i] + impulse[i], p);
            s.target[i] = s.glide[i].destination();
            settle = std::max(settle, s.glide[i].settle_time());
        }
        s.end = t.count() + settle;
        push(s);
        return *this;
    }

    // Set the value at t, at rest.
    Motion& jump(seconds t, const T& value) {
        Segment s = begin(t, Kind::hold);
        s.x0 = lanes<T>::to(value);
        s.v0 = {};
        s.target = s.x0;
        s.end = t.count();
        push(s);
        return *this;
    }

    [[nodiscard]] T value(seconds t) const { return lanes<T>::from(lanes_at(t.count())); }

    [[nodiscard]] lanes_of<T> velocity(seconds t) const {
        const Segment* s = find(t.count());
        return s ? velocity_in(*s, t.count()) : lanes_of<T>{};
    }

    // Where the latest motion ends up.
    [[nodiscard]] T target() const { return lanes<T>::from(segs_.empty() ? initial_ : segs_.back().target); }

    // When the motion active at t comes to rest (absolute time).
    [[nodiscard]] seconds settle_time(seconds t) const {
        const Segment* s = find(t.count());
        return seconds(s ? s->end : 0.0);
    }

    [[nodiscard]] bool settled(seconds t) const {
        const Segment* s = find(t.count());
        return s == nullptr || t.count() >= s->end;
    }

    // Drops motions that ended before t; afterwards the earliest time that can
    // be sampled exactly is the start of the motion active at t.
    void forget_before(seconds t) {
        auto it = std::ranges::upper_bound(segs_, t.count(), {}, &Segment::t0);
        if (it == segs_.begin()) return;
        --it;  // the segment active at t stays
        if (it == segs_.begin()) return;
        initial_ = lanes_at(it->t0);
        segs_.erase(segs_.begin(), it);
    }

    [[nodiscard]] std::size_t segments() const noexcept { return segs_.size(); }

private:
    enum class Kind : std::uint8_t { hold, tween, spring, decay };
    struct Segment {
        Kind        kind = Kind::hold;
        double      t0 = 0;
        double      end = 0;
        lanes_of<T> x0{}, v0{}, target{};
        double      duration = 0;
        Ease        ease{};
        std::array<Oscillator, N> osc{};
        std::array<Glide, N>      glide{};
    };

    const Segment* find(double t) const {
        auto it = std::ranges::upper_bound(segs_, t, {}, &Segment::t0);
        if (it == segs_.begin()) return nullptr;
        return &*(it - 1);
    }

    lanes_of<T> lanes_at(double t) const {
        const Segment* s = find(t);
        if (!s) return initial_;
        double local = t - s->t0;
        lanes_of<T> out{};
        switch (s->kind) {
            case Kind::hold: return s->x0;
            case Kind::tween: {
                if (s->duration <= 0.0 || local >= s->duration) return s->target;
                double u = s->ease(local / s->duration);
                for (int i = 0; i < N; ++i) out[i] = dm::mix(s->x0[i], s->target[i], u);
                return out;
            }
            case Kind::spring:
                for (int i = 0; i < N; ++i) out[i] = s->osc[i].position(local);
                return out;
            case Kind::decay:
                for (int i = 0; i < N; ++i) out[i] = s->glide[i].position(local);
                return out;
        }
        return out;
    }

    lanes_of<T> velocity_in(const Segment& s, double t) const {
        double local = t - s.t0;
        lanes_of<T> v{};
        switch (s.kind) {
            case Kind::hold: break;
            case Kind::tween:
                if (s.duration > 0.0 && local < s.duration) {
                    double k = s.ease.slope(local / s.duration) / s.duration;
                    for (int i = 0; i < N; ++i) v[i] = (s.target[i] - s.x0[i]) * k;
                }
                break;
            case Kind::spring:
                for (int i = 0; i < N; ++i) v[i] = s.osc[i].velocity(local);
                break;
            case Kind::decay:
                for (int i = 0; i < N; ++i) v[i] = s.glide[i].velocity(local);
                break;
        }
        return v;
    }

    Segment begin(seconds t, Kind k) {
        // A motion started in the past replaces the future it would have had.
        auto it = std::ranges::lower_bound(segs_, t.count(), {}, &Segment::t0);
        segs_.erase(it, segs_.end());
        Segment s;
        s.kind = k;
        s.t0 = t.count();
        s.x0 = lanes_at(s.t0);
        const Segment* prev = find(s.t0);
        if (prev) s.v0 = velocity_in(*prev, s.t0);
        return s;
    }

    void push(const Segment& s) { segs_.push_back(s); }

    lanes_of<T>          initial_{};
    std::vector<Segment> segs_;
};

// ── free sampling functions (the form the spec writes) ──────────────────────

template <Animatable T> [[nodiscard]] T sample(const Tween<T>& a, seconds t) { return a.value(t); }
template <Animatable T> [[nodiscard]] T sample(const Track<T>& a, seconds t) { return a.value(t); }
template <Animatable T> [[nodiscard]] T sample(const Spring<T>& a, seconds t) { return a.value(t); }
template <Animatable T> [[nodiscard]] T sample(const Decay<T>& a, seconds t) { return a.value(t); }
template <Animatable T> [[nodiscard]] T sample(const Motion<T>& a, seconds t) { return a.value(t); }

// ── interpolate: map a number through ranges (video authoring's workhorse) ─

enum class Extrapolate : std::uint8_t {
    clamp,     // hold the end values
    extend,    // continue the end segment's line
    identity,  // return the input unchanged outside the range
};

struct InterpolateOptions {
    Ease        ease  = ease::linear;
    Extrapolate left  = Extrapolate::clamp;
    Extrapolate right = Extrapolate::clamp;
};

// `input` must be non-decreasing and as long as `output` (at least 2). An
// input of [0, 30] and an output of [0, 1] maps frame 15 to 0.5.
[[nodiscard]] double interpolate(double x, std::span<const double> input, std::span<const double> output,
                                 InterpolateOptions options = {}) noexcept;

[[nodiscard]] inline double interpolate(double x, std::initializer_list<double> input,
                                        std::initializer_list<double> output,
                                        InterpolateOptions options = {}) noexcept {
    return interpolate(x, std::span(input.begin(), input.size()), std::span(output.begin(), output.size()),
                       options);
}

}  // namespace hanim
