module openmui.audio;

import std;
import hanim;
import openmui.trace;

namespace omui::audio {

namespace dm = hanim::dm;

namespace {

constexpr std::int32_t table_size = 4096;
constexpr std::int32_t control = 16;  // samples between a filter's coefficients

// One sine period, sampled once (dm: the same bits everywhere).
const std::array<float, table_size + 1>& sine_table() {
    static const auto t = [] {
        std::array<float, table_size + 1> v{};
        for (std::int32_t i = 0; i <= table_size; ++i) v[static_cast<std::size_t>(i)] = static_cast<float>(dm::sin(dm::two_pi * i / table_size));
        return v;
    }();
    return t;
}

// A step's aliasing taken off where the phase wraps (polyBLEP).
double blep(double t, double dt) noexcept {
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

// A layer as samples: where it starts in the sound and for how long, its
// envelope's corners, and its glides as a factor a sample.
struct Prepared {
    Layer        layer;
    std::int64_t start = 0, frames = 1;
    double       attack = 1, hold = 0, decay = 1, release = 1;
    double       pitch_step = 1, cutoff_step = 1;  // × a sample
};

struct PreparedSound {
    std::string           name;
    std::vector<Prepared> layers;
    float                 gain = 1;
    std::int32_t          bus = 0;
    std::int64_t          frames = 0;  // the sound's length
};

struct LayerState {
    double        phase = 0, freq = 0, cutoff = 0;
    std::uint32_t noise = 1;
    BiquadState   filter;
    Biquad        coefficients;
    float         left = 1, right = 1;  // the pan's gains
};

struct Voice {
    bool         active = false;
    std::int32_t sound = -1;
    std::int64_t start = 0;  // the clock's sample it began on
    std::uint64_t serial = 0;
    float        gain = 1, pan = 0, pitch = 1;
};

struct Command {
    std::int32_t sound;
    std::int64_t at;
    float        gain, pan, pitch;
};

struct Bus {
    std::string                          name;
    float                                gain = 1;
    std::vector<std::unique_ptr<Effect>> effects;
    std::array<float, block_frames>      left{}, right{};
};

class FilterEffect final : public Effect {
public:
    FilterEffect(FilterKind kind, float cutoff, float q) : kind_(kind), cutoff_(cutoff), q_(q) {}
    void process(std::span<float> left, std::span<float> right, std::int32_t rate) noexcept override {
        if (rate != rate_) coefficients_.set(kind_, cutoff_, q_, rate), rate_ = rate;
        for (float& x : left) x = static_cast<float>(l_.run(coefficients_, x));
        for (float& x : right) x = static_cast<float>(r_.run(coefficients_, x));
    }

private:
    FilterKind  kind_;
    float       cutoff_, q_;
    std::int32_t rate_ = 0;
    Biquad      coefficients_;
    BiquadState l_, r_;
};

class EchoEffect final : public Effect {
public:
    EchoEffect(float time, float feedback, float wet, std::int32_t rate)
        : line_(static_cast<std::size_t>(std::max<std::int64_t>(1, std::lround(time * rate)))), feedback_(feedback), wet_(wet) {}
    void process(std::span<float> left, std::span<float> right, std::int32_t) noexcept override {
        for (std::size_t i = 0; i < left.size(); ++i) {
            auto& d = line_[at_];
            const float l = left[i], r = right[i];
            left[i] = l + wet_ * d[0];
            right[i] = r + wet_ * d[1];
            d = {l + feedback_ * d[0], r + feedback_ * d[1]};
            at_ = at_ + 1 == line_.size() ? 0 : at_ + 1;
        }
    }

private:
    std::vector<std::array<float, 2>> line_;
    std::size_t                       at_ = 0;
    float                             feedback_, wet_;
};

}  // namespace

std::unique_ptr<Effect> filter(FilterKind kind, float cutoff, float q) { return std::make_unique<FilterEffect>(kind, cutoff, q); }
std::unique_ptr<Effect> echo(float time, float feedback, float wet, std::int32_t rate) { return std::make_unique<EchoEffect>(time, feedback, wet, rate); }

float Sound::length() const noexcept {
    float end = 0;
    for (const Layer& l : layers) end = std::max(end, l.at + l.length);
    return end;
}

struct Mixer::Impl {
    Options                    options;
    std::vector<PreparedSound> sounds;
    std::vector<Bus>           buses;
    std::vector<Voice>         voices;
    std::vector<LayerState>    states;  // voices × the most layers a sound has
    std::size_t                layers_per_voice = 0;
    std::uint64_t              serial = 0;
    std::int64_t               clock = 0;
    // The queue from the thread that plays (it writes `tail`) to the one that renders (it writes `head`).
    std::vector<Command>       ring;
    std::atomic<std::uint32_t> head{0}, tail{0};
    std::vector<Command>       pending;  // taken off the queue, waiting for their sample
    std::size_t                waiting = 0;
    std::array<float, block_frames> master_left{}, master_right{};
    double                     limit = 1, release = 0;

    std::int32_t bus_index(std::string_view name) {
        for (std::size_t i = 0; i < buses.size(); ++i)
            if (buses[i].name == name) return static_cast<std::int32_t>(i);
        buses.push_back({std::string(name), 1, {}, {}, {}});
        return static_cast<std::int32_t>(buses.size() - 1);
    }

    void start(const Command& c) {
        // A free voice, else the oldest's place.
        std::size_t slot = voices.size();
        for (std::size_t v = 0; v < voices.size() && slot == voices.size(); ++v)
            if (!voices[v].active) slot = v;
        if (slot == voices.size()) {
            slot = 0;
            for (std::size_t v = 1; v < voices.size(); ++v)
                if (voices[v].serial < voices[slot].serial) slot = v;
        }
        Voice& voice = voices[slot];
        voice = {true, c.sound, std::max(c.at, clock), ++serial, c.gain, c.pan, c.pitch};
        const PreparedSound& s = sounds[static_cast<std::size_t>(c.sound)];
        for (std::size_t l = 0; l < s.layers.size(); ++l) {
            const Prepared& p = s.layers[l];
            LayerState& st = states[slot * layers_per_voice + l];
            st = {};
            st.freq = p.layer.pitch[0] * c.pitch;
            st.cutoff = p.layer.cutoff[0];
            st.noise = (p.layer.seed * 2654435761u) ^ static_cast<std::uint32_t>(voice.serial * 40503u) ^ 0x9e3779b9u;
            if (st.noise == 0) st.noise = 1;
            const double pan = std::clamp(static_cast<double>(p.layer.pan) + c.pan, -1.0, 1.0);
            const double angle = (pan + 1) * dm::pi / 4;  // equal power
            st.left = static_cast<float>(dm::cos(angle) * 1.4142135623730951);
            st.right = static_cast<float>(dm::sin(angle) * 1.4142135623730951);
            st.coefficients.set(p.layer.filter, st.cutoff, p.layer.q, options.rate);
        }
    }

    // One voice's samples [from, to) of the clock into its bus.
    void sound(std::size_t slot, std::int64_t from, std::int64_t to) {
        Voice& voice = voices[slot];
        const PreparedSound& s = sounds[static_cast<std::size_t>(voice.sound)];
        Bus& bus = buses[static_cast<std::size_t>(s.bus)];
        const auto& sine = sine_table();
        const double rate = options.rate;
        for (std::size_t l = 0; l < s.layers.size(); ++l) {
            const Prepared& p = s.layers[l];
            LayerState& st = states[slot * layers_per_voice + l];
            const std::int64_t begin = voice.start + p.start, end = begin + p.frames;
            const std::int64_t a = std::max(from, begin), b = std::min(to, end);
            const float gain = s.gain * p.layer.gain * voice.gain;
            const double k = 1 + 8.0 * p.layer.drive, norm = (1 + k) / k;
            for (std::int64_t i = a; i < b; ++i) {
                const auto t = static_cast<double>(i - begin);
                // The source.
                const double dt = st.freq / rate;
                double x = 0;
                switch (p.layer.wave) {
                    case Wave::sine: {
                        const double at = st.phase * table_size;
                        const auto n = static_cast<std::int32_t>(at);
                        const double f = at - n;
                        x = sine[static_cast<std::size_t>(n)] + (sine[static_cast<std::size_t>(n + 1)] - sine[static_cast<std::size_t>(n)]) * f;
                        break;
                    }
                    case Wave::triangle: x = 1 - 4 * std::abs(st.phase - 0.5); break;
                    case Wave::square:
                        x = (st.phase < 0.5 ? 1.0 : -1.0) + blep(st.phase, dt) - blep(st.phase + 0.5 - (st.phase >= 0.5 ? 1.0 : 0.0), dt);
                        break;
                    case Wave::saw: x = 2 * st.phase - 1 - blep(st.phase, dt); break;
                    case Wave::noise: {
                        st.noise ^= st.noise << 13, st.noise ^= st.noise >> 17, st.noise ^= st.noise << 5;
                        x = static_cast<double>(static_cast<std::int32_t>(st.noise)) / 2147483648.0;
                        break;
                    }
                }
                st.phase += dt;
                if (st.phase >= 1) st.phase -= std::floor(st.phase);
                st.freq *= p.pitch_step;
                // The filter, its cutoff gliding (coefficients every few samples).
                if (p.layer.filter != FilterKind::none) {
                    if ((i - begin) % control == 0) st.coefficients.set(p.layer.filter, st.cutoff, p.layer.q, rate);
                    x = st.filter.run(st.coefficients, x);
                    st.cutoff *= p.cutoff_step;
                }
                // The envelope.
                double e;
                if (t < p.attack) e = t / p.attack;
                else if (t < p.attack + p.hold) e = 1;
                else if (t < p.attack + p.hold + p.decay) e = 1 - (1 - p.layer.envelope.sustain) * (t - p.attack - p.hold) / p.decay;
                else e = p.layer.envelope.sustain;
                const double left_frames = static_cast<double>(p.frames) - t;
                if (left_frames < p.release) e *= left_frames / p.release;
                x *= e;
                if (p.layer.drive > 0) x = k * x / (1 + k * std::abs(x)) * norm;
                const auto at = static_cast<std::size_t>(i - from);
                const auto y = static_cast<float>(x) * gain;
                bus.left[at] += y * st.left;
                bus.right[at] += y * st.right;
            }
        }
        if (to >= voice.start + s.frames) voice.active = false;
    }

    void block(std::span<float> out, std::int64_t frames) {
        const std::int64_t to = clock + frames;
        // Plays taken off the queue; those whose sample is in this block (or past) begin.
        for (std::uint32_t h = head.load(std::memory_order_relaxed), t = tail.load(std::memory_order_acquire); h != t; ++h) {
            if (waiting < pending.size()) pending[waiting++] = ring[h % ring.size()];
            head.store(h + 1, std::memory_order_release);
        }
        for (Bus& b : buses) b.left.fill(0), b.right.fill(0);
        for (std::size_t w = 0; w < waiting;) {
            if (pending[w].at < to) {
                start(pending[w]);
                pending[w] = pending[--waiting];
            } else {
                ++w;
            }
        }
        for (std::size_t v = 0; v < voices.size(); ++v)
            if (voices[v].active) sound(v, clock, to);
        master_left.fill(0), master_right.fill(0);
        const auto n = static_cast<std::size_t>(frames);
        for (Bus& b : buses) {
            for (const auto& e : b.effects) e->process(std::span(b.left).first(n), std::span(b.right).first(n), options.rate);
            for (std::size_t i = 0; i < n; ++i) master_left[i] += b.left[i] * b.gain, master_right[i] += b.right[i] * b.gain;
        }
        // The limiter: down at once to hold a peak under the ceiling, back up slowly.
        for (std::size_t i = 0; i < n; ++i) {
            const double peak = std::max(std::abs(static_cast<double>(master_left[i])), std::abs(static_cast<double>(master_right[i])));
            const double want = peak > options.ceiling ? options.ceiling / peak : 1.0;  // the most this peak allows
            limit = want < limit ? want : limit + (want - limit) * release;
            out[2 * i] = static_cast<float>(master_left[i] * limit);
            out[2 * i + 1] = static_cast<float>(master_right[i] * limit);
        }
        clock = to;
        omui::trace::count("audio.voices", static_cast<double>(std::ranges::count_if(voices, [](const Voice& v) { return v.active; })), "audio");
    }
};

Mixer::Mixer(Options options) : impl_(std::make_unique<Impl>()) {
    Impl& m = *impl_;
    m.options = options;
    m.voices.resize(static_cast<std::size_t>(std::max(1, options.voices)));
    std::uint32_t ring = 1;
    while (ring < static_cast<std::uint32_t>(std::max(2, options.queue))) ring <<= 1;
    m.ring.resize(ring);
    m.pending.resize(ring);
    m.bus_index("sfx");
    m.bus_index("music");
    m.release = 1 - dm::exp(-1.0 / (0.08 * options.rate));  // back up over ~80 ms
}

Mixer::~Mixer() = default;

std::int32_t Mixer::add(const Sound& sound) {
    Impl& m = *impl_;
    const double rate = m.options.rate;
    PreparedSound p{sound.name, {}, sound.gain, m.bus_index(sound.bus), 0};
    for (const Layer& l : sound.layers) {
        Prepared q;
        q.layer = l;
        q.start = std::lround(static_cast<double>(l.at) * rate);
        q.frames = std::max<std::int64_t>(1, std::lround(static_cast<double>(l.length) * rate));
        q.attack = std::max(1.0, static_cast<double>(l.envelope.attack) * rate);
        q.hold = std::max(0.0, static_cast<double>(l.envelope.hold) * rate);
        q.decay = std::max(1.0, static_cast<double>(l.envelope.decay) * rate);
        q.release = std::max(1.0, static_cast<double>(l.envelope.release) * rate);
        const auto glide = [&](float a, float b) { return a > 0 && b > 0 ? dm::exp(dm::log(static_cast<double>(b) / a) / static_cast<double>(q.frames)) : 1.0; };
        q.pitch_step = glide(l.pitch[0], l.pitch[1]);
        q.cutoff_step = glide(l.cutoff[0], l.cutoff[1]);
        p.frames = std::max(p.frames, q.start + q.frames);
        p.layers.push_back(q);
    }
    // Room for its layers in every voice (made now, never while playing).
    if (p.layers.size() > m.layers_per_voice) {
        m.layers_per_voice = p.layers.size();
        m.states.assign(m.voices.size() * m.layers_per_voice, {});
        for (Voice& v : m.voices) v.active = false;
    }
    for (std::size_t i = 0; i < m.sounds.size(); ++i)
        if (m.sounds[i].name == sound.name) {
            m.sounds[i] = std::move(p);
            return static_cast<std::int32_t>(i);
        }
    m.sounds.push_back(std::move(p));
    return static_cast<std::int32_t>(m.sounds.size() - 1);
}

std::int32_t Mixer::find(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < impl_->sounds.size(); ++i)
        if (impl_->sounds[i].name == name) return static_cast<std::int32_t>(i);
    return -1;
}

void Mixer::bus_gain(std::string_view bus, float gain) { impl_->buses[static_cast<std::size_t>(impl_->bus_index(bus))].gain = gain; }

void Mixer::add_effect(std::string_view bus, std::unique_ptr<Effect> effect) {
    impl_->buses[static_cast<std::size_t>(impl_->bus_index(bus))].effects.push_back(std::move(effect));
}

bool Mixer::play(std::int32_t sound, std::int64_t at, float gain, float pan, float pitch) noexcept {
    Impl& m = *impl_;
    if (sound < 0 || static_cast<std::size_t>(sound) >= m.sounds.size()) return false;
    const std::uint32_t t = m.tail.load(std::memory_order_relaxed);
    if (t - m.head.load(std::memory_order_acquire) >= m.ring.size()) return false;
    m.ring[t % m.ring.size()] = {sound, at, gain, pan, pitch};
    m.tail.store(t + 1, std::memory_order_release);
    return true;
}

void Mixer::render(std::span<float> out) noexcept {
    omui::trace::Span span("audio.render", "audio");
    std::int64_t frames = static_cast<std::int64_t>(out.size() / 2);
    std::size_t at = 0;
    while (frames > 0) {
        const std::int64_t n = std::min<std::int64_t>(frames, block_frames);
        impl_->block(out.subspan(at, static_cast<std::size_t>(2 * n)), n);
        at += static_cast<std::size_t>(2 * n);
        frames -= n;
    }
}

std::int64_t Mixer::clock() const noexcept { return impl_->clock; }
std::int32_t Mixer::rate() const noexcept { return impl_->options.rate; }

std::int64_t Mixer::begun() const noexcept { return static_cast<std::int64_t>(impl_->serial); }

std::int32_t Mixer::sounding() const noexcept {
    return static_cast<std::int32_t>(std::ranges::count_if(impl_->voices, [](const Voice& v) { return v.active; }));
}

std::vector<float> render(Mixer& mixer, std::int64_t frames) {
    std::vector<float> out(static_cast<std::size_t>(2 * frames));
    mixer.render(out);
    return out;
}

std::uint64_t hash(std::span<const float> samples) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const float s : samples) {
        const auto bits = std::bit_cast<std::uint32_t>(s);
        for (int b = 0; b < 4; ++b) {
            h ^= (bits >> (8 * b)) & 0xff;
            h *= 0x100000001b3ull;
        }
    }
    return h;
}

float peak(std::span<const float> samples) noexcept {
    float p = 0;
    for (const float s : samples) p = std::max(p, std::abs(s));
    return p;
}

std::vector<std::byte> wav(std::span<const float> interleaved, std::int32_t rate, bool float32) {
    std::vector<std::byte> out;
    auto put = [&](std::uint32_t v, int bytes) {
        for (int b = 0; b < bytes; ++b) out.push_back(static_cast<std::byte>((v >> (8 * b)) & 0xff));
    };
    auto tag = [&](std::string_view s) {
        for (const char c : s) out.push_back(static_cast<std::byte>(c));
    };
    const std::uint32_t bytes_per = float32 ? 4 : 2, data = static_cast<std::uint32_t>(interleaved.size()) * bytes_per;
    tag("RIFF");
    put(36 + data, 4);
    tag("WAVE");
    tag("fmt ");
    put(16, 4);
    put(float32 ? 3 : 1, 2);  // IEEE float, or PCM
    put(2, 2);
    put(static_cast<std::uint32_t>(rate), 4);
    put(static_cast<std::uint32_t>(rate) * 2 * bytes_per, 4);
    put(2 * bytes_per, 2);
    put(bytes_per * 8, 2);
    tag("data");
    put(data, 4);
    for (const float s : interleaved) {
        if (float32) {
            put(std::bit_cast<std::uint32_t>(s), 4);
        } else {
            const auto v = static_cast<std::int16_t>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
            put(static_cast<std::uint16_t>(v), 2);
        }
    }
    return out;
}

}  // namespace omui::audio
