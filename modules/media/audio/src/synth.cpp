// An instrument's voices, sample by sample: an analog-style stack of
// band-limited oscillators, two-operator FM, a Karplus–Strong string (its
// delay tuned to the loop filter's phase delay, read with Hermite
// interpolation so it can slide and wobble), and a drum (a falling tone, the
// membrane's modes, noise and metal through a fixed filter, a click). The
// curves are factors a sample made once per patch; filters are set every few
// samples; nothing allocates.
module openmui.audio;

import std;
import hanim;
import :dsp;
import :synth;

namespace omui::audio::synth {

namespace {

using namespace dsp;
namespace dm = hanim::dm;

constexpr double middle_c = 261.6255653005986;
constexpr std::array<double, 2> mode_ratio{1.593, 2.135};  // a circular membrane's (1,1) and (2,1) modes
constexpr std::array<double, 2> mode_level{0.6, 0.35};
constexpr std::array<double, 6> metal_ratio{1, 1.4827, 1.8003, 2.5461, 2.6303, 3.8968};  // a cymbal's six (the TR-808's)

Curve curve(const Adsr& a, double rate) {
    return {1.0 / std::max(1.0, static_cast<double>(a.attack) * rate), fall(a.decay, rate), std::clamp(static_cast<double>(a.sustain), 0.0, 1.0),
            fall(a.release, rate)};
}

void step(std::int32_t& stage, double& level, const Curve& c, double release) noexcept {
    switch (stage) {
        case 0:
            level += c.attack;
            if (level >= 1) level = 1, stage = 1;
            break;
        case 1: level = c.sustain + (level - c.sustain) * c.decay; break;
        default: level *= release; break;
    }
}

double wrap(double phase) noexcept { return phase >= 1 ? phase - std::floor(phase) : phase; }

}  // namespace

Tone prepare(const Patch& p, std::int32_t rate_) {
    const double rate = rate_;
    Tone t;
    t.patch = p;
    t.rate = rate;
    t.amp = curve(p.amp, rate);
    t.filter = curve(p.filter.adsr, rate);
    if (p.engine == Engine::drum) t.amp.sustain = 1, t.amp.decay = 1;  // a drum's envelope is only its attack: its parts fall on their own
    t.unison = p.engine == Engine::analog ? std::clamp(p.osc.unison, 1, 7) : 1;
    const double n = t.unison;
    for (std::int32_t u = 0; u < t.unison; ++u) {
        const double along = t.unison == 1 ? 0.5 : u / (n - 1);  // 0 … 1 across the unison
        t.ratio[static_cast<std::size_t>(u)] = exp2(static_cast<double>(p.osc.detune) * (along - 0.5) / 1200);
        const double pan = t.unison == 1 ? 0 : static_cast<double>(p.osc.spread) * (2 * along - 1);
        const double angle = (pan + 1) * dm::pi / 4;
        const double norm = 1.4142135623730951 / dm::sqrt(n);
        t.left[static_cast<std::size_t>(u)] = dm::cos(angle) * norm;
        t.right[static_cast<std::size_t>(u)] = dm::sin(angle) * norm;
        t.phase[static_cast<std::size_t>(u)] = u == 0 ? 0 : wrap(u * 0.6180339887498949 + 0.1);
    }
    t.stereo = t.unison > 1 && p.osc.spread > 0;
    t.pwm_step = static_cast<double>(p.osc.pwm_rate) / rate;
    t.vibrato_step = static_cast<double>(p.vibrato.rate) * control / rate;
    t.vibrato_delay = std::max(1.0, static_cast<double>(p.vibrato.delay) * rate);
    t.index_fall = fall(p.fm.decay, rate);
    t.pole = 0.15 + 0.85 * std::clamp(static_cast<double>(p.pluck.brightness), 0.0, 1.0);
    t.string_decay = std::max(0.05, static_cast<double>(p.pluck.decay));
    t.sweep = dm::exp(-1.0 / std::max(1.0, static_cast<double>(p.drum.sweep) * rate));
    t.tone_fall = fall(p.drum.decay, rate);
    t.noise_fall = fall(p.drum.noise_decay, rate);
    t.click_fall = fall(0.004, rate);
    t.mode_fall = {fall(p.drum.decay * 0.55, rate), fall(p.drum.decay * 0.35, rate)};
    t.burst_gap = std::max<std::int64_t>(1, std::lround(0.01 * rate));
    t.glide = std::max<std::int64_t>(1, std::lround(static_cast<double>(p.glide) * rate));
    t.drive_k = 1 + 8.0 * p.drive;
    t.drive_norm = (1 + t.drive_k) / t.drive_k;
    t.choke_fall = fall(0.015, rate);
    if (p.engine == Engine::drum) t.shell.set(p.filter.kind, p.filter.cutoff, p.filter.q, rate);
    return t;
}

std::size_t line_size(double lowest, std::int32_t rate) noexcept {
    std::size_t n = 16;
    while (static_cast<double>(n) < rate / std::max(10.0, lowest) + 8) n <<= 1;
    return n;
}

void begin(Voice& v, const Tone& t, double freq, double to, double pitch, double velocity, std::int64_t start, std::int64_t length,
           std::uint64_t serial, std::uint32_t seed) noexcept {
    const Patch& p = t.patch;
    const bool again = v.active;  // rings into the new note
    if (freq <= 0) freq = (p.engine == Engine::drum ? static_cast<double>(p.drum.pitch) : middle_c) * pitch;
    v.active = true;
    v.released = false;
    v.serial = serial;
    v.start = start;
    v.off = start + std::max<std::int64_t>(1, length);
    v.age = 0;
    v.freq = freq;
    v.slide = 1;
    v.sliding = 0;
    if (to > 0) {
        v.sliding = t.glide;
        v.slide = dm::exp(dm::log(to / freq) / static_cast<double>(t.glide));
    }
    v.velocity = velocity;
    v.stage = 0, v.filter_stage = 0;
    v.fall = t.amp.release;
    v.vibrato = 0, v.wobble = 1;
    v.noise = seed ^ 0x9e3779b9u;
    if (v.noise == 0) v.noise = 1;
    if (!again) {
        v.env = 0, v.filter_env = 0;
        v.phase = t.phase;
        v.sub = 0, v.pwm = 0;
        v.fl = {}, v.fr = {};
        v.mod = 0, v.carrier = 0, v.last = 0;
        v.lowpass = 0, v.dc_in = 0, v.dc_out = 0;
    }
    v.index = static_cast<double>(p.fm.index) * (0.4 + 0.6 * velocity) - p.fm.sustain;
    switch (p.engine) {
        case Engine::pluck: {
            if (v.line.size() < 16) {
                v.active = false;
                return;
            }
            // The loop: a delay and a one-pole lowpass, the delay shortened by the filter's own (so the note is in tune) and the loss
            // set so the fundamental falls 60 dB in the string's decay.
            const double w = dm::two_pi * freq / t.rate, a = 1 - t.pole;
            const double re = 1 - a * dm::cos(w), im = a * dm::sin(w);
            v.tau = dm::atan2(im, re) / w;
            const double gain = t.pole / dm::sqrt(re * re + im * im);
            v.loss = std::min(0.99999, dm::exp(-6.907755278982137 / (freq * t.string_decay)) / gain);
            // The pluck: lowpassed noise (darker when soft), less what the pick's place cancels, its mean taken off, at a set loudness.
            const std::size_t n = v.line.size(), mask = n - 1;
            const double delay = std::clamp(t.rate / freq - v.tau, 3.0, static_cast<double>(n) - 4);
            const auto length_ = std::min(n - 4, static_cast<std::size_t>(delay) + 3);
            const double c = 0.1 + 0.9 * std::clamp(static_cast<double>(p.pluck.brightness), 0.0, 1.0) * (0.55 + 0.45 * velocity);
            const std::size_t from = v.at + n - length_;
            double e = 0;
            for (std::size_t k = 0; k < length_; ++k) {
                e += c * (noise(v.noise) - e);
                v.line[(from + k) & mask] = static_cast<float>(e);
            }
            const auto pick = static_cast<std::size_t>(std::lround(std::clamp(static_cast<double>(p.pluck.pick), 0.0, 0.5) * delay));
            if (pick > 0)
                for (std::size_t k = length_; k-- > pick;) v.line[(from + k) & mask] -= v.line[(from + k - pick) & mask];
            double mean = 0, power = 0;
            for (std::size_t k = 0; k < length_; ++k) mean += v.line[(from + k) & mask];
            mean /= static_cast<double>(length_);
            for (std::size_t k = 0; k < length_; ++k) {
                const double x = v.line[(from + k) & mask] - mean;
                power += x * x;
            }
            const double scale = power > 0 ? 0.6 / dm::sqrt(power / static_cast<double>(length_)) : 0;
            for (std::size_t k = 0; k < length_; ++k) v.line[(from + k) & mask] = static_cast<float>((v.line[(from + k) & mask] - mean) * scale);
            break;
        }
        case Engine::drum: {
            const Drum& d = p.drum;
            v.excess = std::max(0.0, static_cast<double>(d.from) - 1);
            v.tone = d.tone > 0 ? 1 : 0;
            v.mode = d.modes > 0 ? std::array<double, 2>{1, 1} : std::array<double, 2>{0, 0};
            v.shell = d.noise > 0 || d.metal > 0 ? 1 : 0;
            v.click = d.click > 0 ? 1 : 0;
            v.bursts = std::max(0, d.bursts - 1);
            if (!again) v.tone_phase = 0, v.mode_phase = {}, v.metal = {}, v.shell_state = {};
            break;
        }
        default: break;
    }
}

void choke(Voice& v, const Tone& t) noexcept {
    if (!v.active) return;
    v.released = true;
    v.stage = 2;
    v.fall = t.choke_fall;
    v.off = v.start;  // a drum let go too
}

void sound(Voice& v, const Tone& t, std::int64_t clock, std::int32_t frames, float* left, float* right) noexcept {
    const Patch& p = t.patch;
    const double rate = t.rate;
    const bool filtered = p.engine != Engine::drum && p.filter.kind != FilterKind::none;
    const double gain = static_cast<double>(p.gain) * v.velocity;
    for (std::int64_t i = std::max<std::int64_t>(0, v.start - clock); i < frames; ++i) {
        if (!v.released && clock + i >= v.off && p.engine != Engine::drum) v.released = true, v.stage = 2, v.filter_stage = 2;
        // At the control rate: the vibrato's factor and the filter's coefficients.
        if (v.age % control == 0) {
            if (p.vibrato.depth > 0) {
                const double depth = static_cast<double>(p.vibrato.depth) * std::min(1.0, static_cast<double>(v.age) / t.vibrato_delay);
                v.wobble = exp2(depth * sine(v.vibrato) / 1200);
                v.vibrato = wrap(v.vibrato + t.vibrato_step);
            }
            if (filtered) {
                double octaves = static_cast<double>(p.filter.envelope) * v.filter_env * (0.5 + 0.5 * v.velocity);
                if (p.filter.keytrack != 0) octaves += static_cast<double>(p.filter.keytrack) * dm::log(v.freq / middle_c) / dm::ln2;
                v.coefficients.set(p.filter.kind, static_cast<double>(p.filter.cutoff) * exp2(octaves), p.filter.q, rate);
            }
        }
        if (v.sliding > 0) v.freq *= v.slide, --v.sliding;
        const double f = v.freq * v.wobble;
        double l = 0, r = 0;
        bool done = false;
        switch (p.engine) {
            case Engine::analog: {
                const Osc& o = p.osc;
                double width = o.width;
                if (o.wave == Wave::square && o.pwm > 0) {
                    width = std::clamp(width + static_cast<double>(o.pwm) * sine(v.pwm), 0.05, 0.95);
                    v.pwm = wrap(v.pwm + t.pwm_step);
                }
                for (std::size_t u = 0; u < static_cast<std::size_t>(t.unison); ++u) {
                    const double dt = f * t.ratio[u] / rate, ph = v.phase[u];
                    double x = 0;
                    switch (o.wave) {
                        case Wave::saw: x = 2 * ph - 1 - blep(ph, dt); break;
                        case Wave::square:  // a pulse, its mean (2 width − 1) taken off so a narrow one carries no DC
                            x = (ph < width ? 1.0 : -1.0) + blep(ph, dt) - blep(wrap(ph + 1 - width), dt) - (2 * width - 1);
                            break;
                        case Wave::triangle: x = 1 - 4 * std::abs(ph - 0.5); break;
                        case Wave::sine: x = sine(ph); break;
                        case Wave::noise: x = noise(v.noise); break;
                        case Wave::recording: break;  // a sound layer's, never an oscillator's (the piece's reader takes none)
                    }
                    v.phase[u] = wrap(ph + dt);
                    l += x * t.left[u];
                    r += x * t.right[u];
                }
                if (o.sub > 0) {
                    const double s = sine(v.sub) * o.sub;
                    v.sub = wrap(v.sub + f * 0.5 / rate);
                    l += s, r += s;
                }
                if (o.noise > 0) {
                    const double n = noise(v.noise) * o.noise;
                    l += n, r += n;
                }
                break;
            }
            case Engine::fm: {
                const Fm& m = p.fm;
                const double mod = sine_any(v.mod + static_cast<double>(m.feedback) * 0.24 * v.last);
                v.last = mod;
                l = sine_any(v.carrier + (static_cast<double>(m.sustain) + v.index) * mod / dm::two_pi);
                v.index *= t.index_fall;
                v.mod = wrap(v.mod + f * m.ratio / rate);
                v.carrier = wrap(v.carrier + f / rate);
                break;
            }
            case Engine::pluck: {
                const std::size_t n = v.line.size(), mask = n - 1;
                const double delay = std::clamp(rate / f - v.tau, 3.0, static_cast<double>(n) - 4);
                const double back = static_cast<double>(v.at + n) - delay;
                const auto k = static_cast<std::size_t>(back);
                const double fr = back - static_cast<double>(k);
                const double y0 = v.line[(k - 1) & mask], y1 = v.line[k & mask], y2 = v.line[(k + 1) & mask], y3 = v.line[(k + 2) & mask];
                const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
                const double y = ((c3 * fr + c2) * fr + c1) * fr + y1;
                v.lowpass += t.pole * (y - v.lowpass);
                v.line[v.at] = flush(static_cast<float>(v.lowpass * v.loss));
                v.at = (v.at + 1) & mask;
                const double o = y - v.dc_in + 0.995 * v.dc_out;
                v.dc_in = y, v.dc_out = flush(o);
                l = o;
                done = static_cast<double>(v.age) > t.string_decay * 1.4 * rate;
                break;
            }
            case Engine::drum: {
                const Drum& d = p.drum;
                const double fr = f * (1 + v.excess);
                v.excess *= t.sweep;
                double x = 0;
                if (v.tone > 0) {
                    x = d.tone * v.tone * sine(v.tone_phase);
                    v.tone = flush(v.tone * t.tone_fall);
                }
                v.tone_phase = wrap(v.tone_phase + fr / rate);
                if (d.modes > 0) {
                    for (std::size_t m = 0; m < 2; ++m) {
                        x += d.modes * mode_level[m] * v.mode[m] * sine(v.mode_phase[m]);
                        v.mode_phase[m] = wrap(v.mode_phase[m] + fr * mode_ratio[m] / rate);
                        v.mode[m] = flush(v.mode[m] * t.mode_fall[m]);
                    }
                }
                if (v.shell > 0 || v.bursts > 0) {
                    if (v.bursts > 0 && v.age > 0 && v.age % t.burst_gap == 0) v.shell = 1, --v.bursts;
                    double s = d.noise > 0 ? d.noise * noise(v.noise) : 0.0;
                    if (d.metal > 0) {
                        double m = 0;
                        for (std::size_t j = 0; j < 6; ++j) {
                            m += v.metal[j] < 0.5 ? 1.0 : -1.0;
                            v.metal[j] = wrap(v.metal[j] + v.freq * metal_ratio[j] / rate);
                        }
                        s += d.metal * m / 3;
                    }
                    if (p.filter.kind != FilterKind::none) s = v.shell_state.run(t.shell, s);
                    x += s * v.shell;
                    v.shell = flush(v.shell * t.noise_fall);
                }
                if (v.click > 0) {
                    x += d.click * v.click * noise(v.noise);
                    v.click = flush(v.click * t.click_fall);
                }
                l = x;
                done = v.tone < 1e-4 && v.mode[0] < 1e-4 && v.shell < 1e-4 && v.bursts == 0 && v.click < 1e-4;
                break;
            }
        }
        if (filtered) {
            l = v.fl.run(v.coefficients, l);
            r = t.stereo ? v.fr.run(v.coefficients, r) : l;
        } else if (!t.stereo) {
            r = l;
        }
        step(v.stage, v.env, t.amp, v.fall);
        if (p.filter.envelope != 0) step(v.filter_stage, v.filter_env, t.filter, t.filter.release);
        if (p.drive > 0) {
            l = t.drive_k * l / (1 + t.drive_k * std::abs(l)) * t.drive_norm;
            r = t.drive_k * r / (1 + t.drive_k * std::abs(r)) * t.drive_norm;
        }
        const double g = gain * v.env;
        left[i] += static_cast<float>(l * g);
        right[i] += static_cast<float>(r * g);
        ++v.age;
        if (done || (v.stage == 2 && v.env < 1e-4) || (v.stage == 1 && t.amp.sustain == 0 && v.env < 1e-4)) {
            v.active = false;
            break;
        }
    }
}

}  // namespace omui::audio::synth
