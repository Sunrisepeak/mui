// openmui.audio:dsp — the building blocks every voice and effect shares (not
// exported): a sine table, band-limited steps, biquads, seeded noise, and
// the curves an envelope falls by. Each is written out with the five
// correctly rounded operations and hanim's transcendentals, so a sample is
// the same bits on every platform (the package is built with
// -ffp-contract=off and no fast-math).
module openmui.audio:dsp;

import std;
import hanim;
import :mix;

namespace omui::audio::dsp {

namespace dm = hanim::dm;

inline constexpr std::int32_t table_size = 4096;
inline constexpr std::int32_t control = 16;  // samples between a filter's coefficients

// One sine period, sampled once (dm: the same bits everywhere).
const std::array<float, table_size + 1>& sine_table();

// The sine table at `phase` (cycles, 0 ≤ phase < 1), interpolated.
inline double sine(double phase) noexcept {
    const auto& t = sine_table();
    const double at = phase * table_size;
    const auto n = static_cast<std::int32_t>(at);
    const double f = at - n;
    return t[static_cast<std::size_t>(n)] + (t[static_cast<std::size_t>(n + 1)] - t[static_cast<std::size_t>(n)]) * f;
}
// The same at any phase (FM's, a chorus's).
inline double sine_any(double phase) noexcept { return sine(phase - std::floor(phase)); }

// A step's aliasing taken off where the phase wraps (polyBLEP).
inline double blep(double t, double dt) noexcept {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1;
    }
    if (t > 1 - dt) {
        t = (t - 1) / dt;
        return t * t + t + t + 1;
    }
    return 0;
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    void set(FilterKind kind, double cutoff, double q, double rate) noexcept {
        const double fc = std::clamp(cutoff, 20.0, rate * 0.45);
        const double w = dm::two_pi * fc / rate;
        const double c = dm::cos(w), s = dm::sin(w);
        const double alpha = s / (2 * std::max(q, 0.05));
        const double a0 = 1 + alpha;
        switch (kind) {
            case FilterKind::lowpass: b0 = (1 - c) / 2, b1 = 1 - c, b2 = (1 - c) / 2; break;
            case FilterKind::highpass: b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2; break;
            case FilterKind::bandpass: b0 = alpha, b1 = 0, b2 = -alpha; break;
            case FilterKind::none: b0 = a0, b1 = 0, b2 = 0; break;
        }
        b0 /= a0, b1 /= a0, b2 /= a0;
        a1 = -2 * c / a0;
        a2 = (1 - alpha) / a0;
    }
};

struct BiquadState {
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double run(const Biquad& f, double x) noexcept {
        const double y = f.b0 * x + f.b1 * x1 + f.b2 * x2 - f.a1 * y1 - f.a2 * y2;
        x2 = x1, x1 = x, y2 = y1, y1 = y;
        return y;
    }
};

// Seeded white noise, −1…1 (xorshift32).
inline double noise(std::uint32_t& s) noexcept {
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return static_cast<double>(static_cast<std::int32_t>(s)) / 2147483648.0;
}

// The factor a sample that falls 60 dB in `seconds`.
inline double fall(double seconds, double rate) noexcept { return dm::exp(-6.907755278982137 / std::max(1.0, seconds * rate)); }

// 2^x, and a MIDI note's Hz.
inline double exp2(double x) noexcept { return dm::exp(x * dm::ln2); }
inline double hz(double key) noexcept { return 440.0 * exp2((key - 69) / 12.0); }

// Below this a value is taken as zero (denormals are slow, and flushed by
// hand they are flushed the same everywhere).
inline float flush(float x) noexcept { return (x < 1e-20f && x > -1e-20f) ? 0.0f : x; }
inline double flush(double x) noexcept { return (x < 1e-20 && x > -1e-20) ? 0.0 : x; }

}  // namespace omui::audio::dsp
