// The built-in effects: a filter, an echo (straight or from side to side), a
// reverb and a chorus. Each owns its lines from the start and allocates
// nothing while it runs; each is a function of what reached it, the same bits
// everywhere (denormals flushed by hand, so no platform's mode decides).
module openmui.audio;

import std;
import hanim;
import :dsp;

namespace omui::audio {

namespace {

using namespace dsp;

class FilterEffect final : public Effect {
public:
    FilterEffect(FilterKind kind, float cutoff, float q) : kind_(kind), cutoff_(cutoff), q_(q) {}
    void process(std::span<float> left, std::span<float> right, std::int32_t rate) noexcept override {
        if (rate != rate_) coefficients_.set(kind_, cutoff_, q_, rate), rate_ = rate;
        for (float& x : left) x = static_cast<float>(l_.run(coefficients_, x));
        for (float& x : right) x = static_cast<float>(r_.run(coefficients_, x));
    }

private:
    FilterKind   kind_;
    float        cutoff_, q_;
    std::int32_t rate_ = 0;
    Biquad       coefficients_;
    BiquadState  l_, r_;
};

class EchoEffect final : public Effect {
public:
    EchoEffect(float time, float feedback, float wet, float dry, bool pingpong, std::int32_t rate)
        : line_(static_cast<std::size_t>(std::max<std::int64_t>(1, std::lround(time * rate)))), feedback_(feedback), wet_(wet), dry_(dry), pingpong_(pingpong) {}
    void process(std::span<float> left, std::span<float> right, std::int32_t) noexcept override {
        if (pingpong_) {
            // Both sides into the left line; each line feeds the other, so the repeats cross.
            for (std::size_t i = 0; i < left.size(); ++i) {
                auto& d = line_[at_];
                const float l = left[i], r = right[i];
                left[i] = l * dry_ + wet_ * d[0];
                right[i] = r * dry_ + wet_ * d[1];
                d = {flush((l + r) * 0.5f + feedback_ * d[1]), flush(feedback_ * d[0])};
                at_ = at_ + 1 == line_.size() ? 0 : at_ + 1;
            }
            return;
        }
        for (std::size_t i = 0; i < left.size(); ++i) {
            auto& d = line_[at_];
            const float l = left[i], r = right[i];
            left[i] = l * dry_ + wet_ * d[0];
            right[i] = r * dry_ + wet_ * d[1];
            d = {l + feedback_ * d[0], r + feedback_ * d[1]};
            at_ = at_ + 1 == line_.size() ? 0 : at_ + 1;
        }
    }

private:
    std::vector<std::array<float, 2>> line_;
    std::size_t                       at_ = 0;
    float                             feedback_, wet_, dry_;
    bool                              pingpong_;
};

// Freeverb (Jezar at Dreampoint's, public domain): eight damped combs in
// parallel then four allpasses in series, a channel each, the right's lines
// a little longer; before them a pre-delay.
class ReverbEffect final : public Effect {
public:
    ReverbEffect(const EffectSpec& s, std::int32_t rate) {
        constexpr std::array<int, 8> combs{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        constexpr std::array<int, 4> passes{556, 441, 341, 225};
        constexpr int stereo = 23;
        const double scale = rate / 44100.0;
        auto len = [&](int n) { return static_cast<std::size_t>(std::max<std::int64_t>(1, std::lround(n * scale))); };
        for (std::size_t c = 0; c < 2; ++c) {
            for (std::size_t i = 0; i < 8; ++i) comb_[c][i].line.assign(len(combs[i] + (c ? stereo : 0)), 0.0f);
            for (std::size_t i = 0; i < 4; ++i) pass_[c][i].line.assign(len(passes[i] + (c ? stereo : 0)), 0.0f);
        }
        pre_.assign(static_cast<std::size_t>(std::max<std::int64_t>(1, std::lround(static_cast<double>(s.predelay) * rate))), {0.0f, 0.0f});
        feedback_ = 0.7f + 0.28f * std::clamp(s.size, 0.0f, 1.0f);
        damp_ = 0.4f * std::clamp(s.damping, 0.0f, 1.0f);
        const float wet = s.wet * 3, width = std::clamp(s.width, 0.0f, 1.0f);
        wet1_ = wet * (width / 2 + 0.5f);
        wet2_ = wet * ((1 - width) / 2);
        dry_ = s.dry;
    }
    void process(std::span<float> left, std::span<float> right, std::int32_t) noexcept override {
        for (std::size_t i = 0; i < left.size(); ++i) {
            auto& p = pre_[pre_at_];
            const float in = (p[0] + p[1]) * 0.015f;
            p = {left[i], right[i]};
            pre_at_ = pre_at_ + 1 == pre_.size() ? 0 : pre_at_ + 1;
            std::array<float, 2> out{0.0f, 0.0f};
            for (std::size_t c = 0; c < 2; ++c) {
                for (Comb& k : comb_[c]) {
                    const float y = k.line[k.at];
                    k.store = flush(y * (1 - damp_) + k.store * damp_);
                    k.line[k.at] = flush(in + k.store * feedback_);
                    k.at = k.at + 1 == k.line.size() ? 0 : k.at + 1;
                    out[c] += y;
                }
                for (Pass& a : pass_[c]) {
                    const float b = a.line[a.at];
                    const float y = b - out[c];
                    a.line[a.at] = flush(out[c] + b * 0.5f);
                    a.at = a.at + 1 == a.line.size() ? 0 : a.at + 1;
                    out[c] = y;
                }
            }
            left[i] = left[i] * dry_ + out[0] * wet1_ + out[1] * wet2_;
            right[i] = right[i] * dry_ + out[1] * wet1_ + out[0] * wet2_;
        }
    }

private:
    struct Comb {
        std::vector<float> line;
        std::size_t        at = 0;
        float              store = 0;
    };
    struct Pass {
        std::vector<float> line;
        std::size_t        at = 0;
    };
    std::array<std::array<Comb, 8>, 2> comb_;
    std::array<std::array<Pass, 4>, 2> pass_;
    std::vector<std::array<float, 2>>  pre_;
    std::size_t                        pre_at_ = 0;
    float                              feedback_ = 0.84f, damp_ = 0.2f, wet1_ = 1, wet2_ = 0, dry_ = 1;
};

// A chorus: each side read back from its own line at a delay that swings
// (the sides a quarter turn apart, the voices spread over the turn).
class ChorusEffect final : public Effect {
public:
    ChorusEffect(const EffectSpec& s, std::int32_t rate)
        : delay_(static_cast<double>(s.delay) * rate), depth_(static_cast<double>(s.depth) * rate), step_(static_cast<double>(s.rate) / rate),
          voices_(std::clamp(s.voices, 1, 4)), wet_(s.wet / static_cast<float>(std::clamp(s.voices, 1, 4))), dry_(s.dry) {
        std::size_t n = 4;
        while (static_cast<double>(n) < delay_ + depth_ + 4) n <<= 1;
        line_.assign(n, {0.0f, 0.0f});
    }
    void process(std::span<float> left, std::span<float> right, std::int32_t) noexcept override {
        const std::size_t mask = line_.size() - 1;
        for (std::size_t i = 0; i < left.size(); ++i) {
            line_[at_] = {left[i], right[i]};
            std::array<double, 2> sum{0, 0};
            for (std::int32_t v = 0; v < voices_; ++v) {
                for (std::size_t c = 0; c < 2; ++c) {
                    const double d = std::max(1.0, delay_ + depth_ * sine_any(phase_ + static_cast<double>(v) / voices_ + (c ? 0.25 : 0.0)));
                    const double back = static_cast<double>(at_ + line_.size()) - d;
                    const auto n = static_cast<std::size_t>(back);
                    const double f = back - static_cast<double>(n);
                    const float a = line_[n & mask][c], b = line_[(n + 1) & mask][c];
                    sum[c] += a + (b - a) * f;
                }
            }
            left[i] = left[i] * dry_ + static_cast<float>(sum[0]) * wet_;
            right[i] = right[i] * dry_ + static_cast<float>(sum[1]) * wet_;
            at_ = (at_ + 1) & mask;
            phase_ += step_;
            if (phase_ >= 1) phase_ -= 1;
        }
    }

private:
    std::vector<std::array<float, 2>> line_;
    std::size_t                       at_ = 0;
    double                            phase_ = 0, delay_, depth_, step_;
    std::int32_t                      voices_;
    float                             wet_, dry_;
};

}  // namespace

std::unique_ptr<Effect> filter(FilterKind kind, float cutoff, float q) { return std::make_unique<FilterEffect>(kind, cutoff, q); }
std::unique_ptr<Effect> echo(float time, float feedback, float wet, std::int32_t rate) {
    return std::make_unique<EchoEffect>(time, feedback, wet, 1.0f, false, rate);
}

std::unique_ptr<Effect> effect(const EffectSpec& s, std::int32_t rate, double beat) {
    switch (s.kind) {
        case EffectKind::lowpass: return filter(FilterKind::lowpass, s.cutoff, s.q);
        case EffectKind::highpass: return filter(FilterKind::highpass, s.cutoff, s.q);
        case EffectKind::bandpass: return filter(FilterKind::bandpass, s.cutoff, s.q);
        case EffectKind::echo: {
            const auto time = static_cast<float>(s.beats > 0 ? s.beats * beat : s.time);
            return std::make_unique<EchoEffect>(time, s.feedback, s.wet, s.dry, s.pingpong, rate);
        }
        case EffectKind::reverb: return std::make_unique<ReverbEffect>(s, rate);
        case EffectKind::chorus: return std::make_unique<ChorusEffect>(s, rate);
    }
    return filter(FilterKind::none, 1000);
}

}  // namespace omui::audio
