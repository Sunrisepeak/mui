// The takes in a recording, and what each is like (audio::takes).
module openmui.audio;

import std;
import hanim;

namespace omui::audio {

namespace {

namespace dm = hanim::dm;

double db(double linear) { return linear > 0 ? 20 * dm::log(linear) / dm::log(10.0) : -200; }

// A window's pitch by normalised autocorrelation (70–700 Hz), 0 when nothing in it repeats clearly.
double pitch_of(std::span<const float> w, std::int32_t rate) {
    const auto lo = static_cast<std::size_t>(rate / 700), hi = std::min(w.size() / 2, static_cast<std::size_t>(rate / 70));
    double best = 0;
    std::size_t at = 0;
    for (std::size_t lag = lo; lag < hi; ++lag) {
        double xy = 0, xx = 0, yy = 0;
        for (std::size_t i = 0; i + lag < w.size(); ++i) {
            xy += static_cast<double>(w[i]) * w[i + lag];
            xx += static_cast<double>(w[i]) * w[i];
            yy += static_cast<double>(w[i + lag]) * w[i + lag];
        }
        const double r = xx > 0 && yy > 0 ? xy / dm::sqrt(xx * yy) : 0;
        if (r > best) best = r, at = lag;
    }
    return best > 0.6 && at > 0 ? static_cast<double>(rate) / static_cast<double>(at) : 0;
}

}  // namespace

std::vector<Take> takes(const Recording& recording, double threshold, double gap, double pad) {
    const std::vector<float>& x = recording.samples;
    const std::int32_t rate = std::max(1, recording.rate);
    const auto hop = static_cast<std::size_t>(std::max(1, rate / 100));  // 10 ms
    // Which 10 ms stretches are loud.
    std::vector<bool> loud((x.size() + hop - 1) / hop);
    for (std::size_t k = 0; k < loud.size(); ++k) {
        double sum = 0;
        const std::size_t a = k * hop, b = std::min(x.size(), a + hop);
        for (std::size_t i = a; i < b; ++i) sum += static_cast<double>(x[i]) * x[i];
        loud[k] = b > a && db(dm::sqrt(sum / static_cast<double>(b - a))) > threshold;
    }
    // Loud stretches, joined across gaps shorter than `gap`.
    std::vector<std::pair<std::size_t, std::size_t>> spans;  // in hops
    const auto join = static_cast<std::size_t>(gap * 100);
    for (std::size_t k = 0; k < loud.size(); ++k) {
        if (!loud[k]) continue;
        if (!spans.empty() && k - spans.back().second <= join) spans.back().second = k + 1;
        else spans.emplace_back(k, k + 1);
    }
    std::vector<Take> out;
    const auto keep = static_cast<std::int64_t>(pad * rate);
    const auto window = static_cast<std::size_t>(rate / 25);  // 40 ms
    for (const auto& [a, b] : spans) {
        Take t;
        t.begin = std::max<std::int64_t>(0, static_cast<std::int64_t>(a * hop) - keep);
        t.end = std::min(static_cast<std::int64_t>(x.size()), static_cast<std::int64_t>(b * hop) + keep);
        const auto take = std::span(x).subspan(static_cast<std::size_t>(t.begin), static_cast<std::size_t>(t.end - t.begin));
        double sum = 0;
        std::size_t crossings = 0;
        for (std::size_t i = 0; i < take.size(); ++i) {
            t.peak = std::max(t.peak, std::abs(take[i]));
            sum += static_cast<double>(take[i]) * take[i];
            if (i > 0 && (take[i - 1] < 0) != (take[i] < 0)) ++crossings;
        }
        t.rms = db(dm::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, take.size()))));
        t.brightness = static_cast<double>(crossings) * rate / static_cast<double>(std::max<std::size_t>(1, take.size()));
        std::vector<double> pitches;
        std::size_t windows = 0;
        for (std::size_t i = 0; i + window <= take.size(); i += window / 2, ++windows)
            if (const double p = pitch_of(take.subspan(i, window), rate); p > 0) pitches.push_back(p);
        if (!pitches.empty()) {
            std::ranges::sort(pitches);
            t.pitch = pitches[pitches.size() / 2];
        }
        t.voiced = windows ? static_cast<double>(pitches.size()) / static_cast<double>(windows) : 0;
        out.push_back(t);
    }
    return out;
}

}  // namespace omui::audio
