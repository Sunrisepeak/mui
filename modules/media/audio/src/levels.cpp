// How loud a mix is and where its energy lies: the peak, the RMS, integrated
// loudness by ITU-R BS.1770-4 (K-weighting, 400 ms blocks overlapping by
// 75%, gated at −70 LUFS and 10 LU under the ungated mean) and five bands
// (Linkwitz–Riley crossovers), with the channels' correlation. Offline, for
// tools and tests; the same numbers everywhere (hanim's transcendentals).
module openmui.audio;

import std;
import hanim;
import :dsp;

namespace omui::audio {

namespace {

namespace dm = hanim::dm;
using dsp::Biquad;
using dsp::BiquadState;

constexpr double ln10 = 2.302585092994046;

double db(double power) noexcept { return power > 0 ? 10 * dm::log(power) / ln10 : -200.0; }
double tan(double x) noexcept { return dm::sin(x) / dm::cos(x); }

// The K-weighting's two stages at `rate` (BS.1770's, from their analogue prototypes).
std::array<Biquad, 2> k_weighting(double rate) {
    std::array<Biquad, 2> k;
    {
        const double gain = 3.999843853973347, q = 0.7071752369554196, fc = 1681.974450955533;
        const double K = tan(dm::pi * fc / rate);
        const double vh = dm::exp(gain / 20 * ln10), vb = dm::pow(vh, 0.4996667741545416);
        const double a0 = 1 + K / q + K * K;
        k[0] = {(vh + vb * K / q + K * K) / a0, 2 * (K * K - vh) / a0, (vh - vb * K / q + K * K) / a0, 2 * (K * K - 1) / a0, (1 - K / q + K * K) / a0};
    }
    {
        const double q = 0.5003270373238773, fc = 38.13547087602444;
        const double K = tan(dm::pi * fc / rate);
        const double a0 = 1 + K / q + K * K;
        k[1] = {1, -2, 1, 2 * (K * K - 1) / a0, (1 - K / q + K * K) / a0};
    }
    return k;
}

}  // namespace

Levels levels(std::span<const float> lr, std::int32_t rate_) {
    Levels out;
    const std::size_t frames = lr.size() / 2;
    if (frames == 0) return out;
    const double rate = rate_;
    double sum = 0, ll = 0, rr = 0, lrs = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double l = lr[2 * i], r = lr[2 * i + 1];
        sum += l * l + r * r;
        ll += l * l, rr += r * r, lrs += l * r;
    }
    out.peak = peak(lr);
    out.rms = db(sum / static_cast<double>(2 * frames));
    out.correlation = ll > 0 && rr > 0 ? lrs / dm::sqrt(ll * rr) : 1.0;

    // Loudness: each channel K-weighted, mean squares over 400 ms blocks every 100 ms, gated.
    const auto k = k_weighting(rate);
    std::array<std::array<BiquadState, 2>, 2> state{};
    const auto hop = static_cast<std::size_t>(std::lround(rate * 0.1));
    std::vector<double> quarters;  // each 100 ms's sum of squares, both channels
    double acc = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t c = 0; c < 2; ++c) {
            const double y = state[c][1].run(k[1], state[c][0].run(k[0], lr[2 * i + c]));
            acc += y * y;
        }
        if ((i + 1) % hop == 0) quarters.push_back(acc), acc = 0;
    }
    std::vector<double> blocks;  // mean squares of 400 ms
    for (std::size_t b = 0; b + 4 <= quarters.size(); ++b)
        blocks.push_back((quarters[b] + quarters[b + 1] + quarters[b + 2] + quarters[b + 3]) / static_cast<double>(4 * hop));
    auto loud = [](double ms) { return -0.691 + db(ms); };
    double mean = 0;
    std::size_t n = 0;
    for (const double b : blocks)
        if (loud(b) > -70) mean += b, ++n;
    if (n > 0) {
        const double gate = loud(mean / static_cast<double>(n)) - 10;
        double gated = 0;
        std::size_t m = 0;
        for (const double b : blocks)
            if (loud(b) > -70 && loud(b) > gate) gated += b, ++m;
        if (m > 0) out.loudness = loud(gated / static_cast<double>(m));
    }

    // Bands: Linkwitz–Riley (two Butterworths) high- and lowpasses at the crossovers.
    constexpr std::array<double, 6> edges{0, 80, 250, 2000, 6000, 0};
    std::array<double, 5> energy{};
    double total = 0;
    for (std::size_t band = 0; band < 5; ++band) {
        Biquad hp, lp;
        if (edges[band] > 0) hp.set(FilterKind::highpass, edges[band], 0.7071067811865476, rate);
        if (edges[band + 1] > 0) lp.set(FilterKind::lowpass, edges[band + 1], 0.7071067811865476, rate);
        std::array<std::array<BiquadState, 4>, 2> s{};
        double e = 0;
        for (std::size_t i = 0; i < frames; ++i)
            for (std::size_t c = 0; c < 2; ++c) {
                double y = lr[2 * i + c];
                if (edges[band] > 0) y = s[c][1].run(hp, s[c][0].run(hp, y));
                if (edges[band + 1] > 0) y = s[c][3].run(lp, s[c][2].run(lp, y));
                e += y * y;
            }
        energy[band] = e;
        total += e;
    }
    for (std::size_t band = 0; band < 5; ++band) out.bands[band] = total > 0 ? db(energy[band] / total) : -200.0;
    return out;
}

}  // namespace omui::audio
