module openmui.audio;

import std;
import openmui.hash;
import hanim;
import openmui.trace;
import :dsp;

namespace omui::audio {

namespace dm = hanim::dm;

using namespace dsp;

const std::array<float, table_size + 1>& dsp::sine_table() {
    static const auto t = [] {
        std::array<float, table_size + 1> v{};
        for (std::int32_t i = 0; i <= table_size; ++i) v[static_cast<std::size_t>(i)] = static_cast<float>(dm::sin(dm::two_pi * i / table_size));
        return v;
    }();
    return t;
}

namespace {

// A layer as samples: where it starts in the sound and for how long, its
// envelope's corners, and its glides as a factor a sample.
struct Recorded {
    std::vector<float> samples;
    std::int32_t       rate = 48000;
};

struct Prepared {
    Layer        layer;
    std::int64_t start = 0, frames = 1;
    double       attack = 1, hold = 0, decay = 1, release = 1;
    double       pitch_step = 1, cutoff_step = 1;  // × a sample (a recording's: its speed's)
    std::shared_ptr<const Recorded> recorded;      // a recording's layer: what it plays (none: silent)
    double       step = 1;                         // its samples a sample of the mixer, at speed 1
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
    std::int32_t                         to = -1;   // routed into this bus (−1: the master)
    std::int32_t                         key = -1;  // ducked under this bus (−1: never)
    Duck                                 duck;
    double                               attack = 0, release = 0, level = 0;  // the ducking's follower
};

}  // namespace

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
    LimiterStats               limited;
    std::vector<std::int32_t>  order;  // the buses, each after those routed into it and its key
    std::vector<BusBlock>      views;  // what sources add into
    std::vector<std::shared_ptr<Source>> sources;
    std::vector<std::pair<std::string, std::shared_ptr<const Recorded>>> recordings;

    std::int32_t bus_index(std::string_view name) {
        for (std::size_t i = 0; i < buses.size(); ++i)
            if (buses[i].name == name) return static_cast<std::int32_t>(i);
        buses.push_back({std::string(name), 1, {}, {}, {}});
        views.clear();
        for (Bus& b : buses) views.push_back({b.left, b.right});
        reorder();
        return static_cast<std::int32_t>(buses.size() - 1);
    }

    // The buses in an order where every bus comes after those routed into it
    // and after its key (with no routes, their own order). One caught in a
    // loop goes last, as it is.
    void reorder() {
        const std::size_t n = buses.size();
        std::vector<std::int32_t> waits(n, 0);
        for (const Bus& b : buses) {
            if (b.to >= 0) ++waits[static_cast<std::size_t>(b.to)];
        }
        for (std::size_t i = 0; i < n; ++i)
            if (buses[i].key >= 0) ++waits[i];
        std::vector<bool> done(n, false);
        order.clear();
        for (bool more = true; more;) {
            more = false;
            for (std::size_t i = 0; i < n; ++i) {
                if (done[i] || waits[i] != 0) continue;
                done[i] = true, more = true;
                order.push_back(static_cast<std::int32_t>(i));
                if (buses[i].to >= 0) --waits[static_cast<std::size_t>(buses[i].to)];
                for (std::size_t j = 0; j < n; ++j)
                    if (buses[j].key == static_cast<std::int32_t>(i)) --waits[j];
                break;  // from the first again: the lowest ready index next
            }
        }
        for (std::size_t i = 0; i < n; ++i)
            if (!done[i]) order.push_back(static_cast<std::int32_t>(i));
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
            st.freq = (p.layer.wave == Wave::recording ? p.layer.speed[0] : p.layer.pitch[0]) * c.pitch;
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
                    case Wave::recording: {  // the phase is the place in the recording (its samples), read between two
                        if (!p.recorded) break;
                        const std::vector<float>& r = p.recorded->samples;
                        const auto n = static_cast<std::size_t>(st.phase);
                        const double f = st.phase - static_cast<double>(n);
                        if (n + 1 < r.size()) x = r[n] + (r[n + 1] - r[n]) * f;
                        else if (n < r.size()) x = r[n] * (1 - f);
                        break;
                    }
                }
                if (p.layer.wave == Wave::recording) {
                    st.phase += st.freq * p.step;
                } else {
                    st.phase += dt;
                    if (st.phase >= 1) st.phase -= std::floor(st.phase);
                }
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
        for (const auto& source : sources) source->render(clock, static_cast<std::int32_t>(frames), views);
        master_left.fill(0), master_right.fill(0);
        const auto n = static_cast<std::size_t>(frames);
        for (const std::int32_t index : order) {
            Bus& b = buses[static_cast<std::size_t>(index)];
            for (const auto& e : b.effects) e->process(std::span(b.left).first(n), std::span(b.right).first(n), options.rate);
            if (b.key >= 0) {
                // Down while its key sounds: a peak follower on the key (after its effects, before its gain), the gain falling with it.
                const Bus& k = buses[static_cast<std::size_t>(b.key)];
                const double depth = b.duck.depth, over = 1.0 / std::max(1e-6, static_cast<double>(b.duck.threshold));
                for (std::size_t i = 0; i < n; ++i) {
                    const double x = std::max(std::abs(static_cast<double>(k.left[i])), std::abs(static_cast<double>(k.right[i])));  // its own level, before its gain
                    b.level += (x - b.level) * (x > b.level ? b.attack : b.release);
                    const auto g = static_cast<float>(1 - depth * std::min(1.0, b.level * over));
                    b.left[i] *= g, b.right[i] *= g;
                }
            }
            if (b.to >= 0) {
                Bus& into = buses[static_cast<std::size_t>(b.to)];
                for (std::size_t i = 0; i < n; ++i) into.left[i] += b.left[i] * b.gain, into.right[i] += b.right[i] * b.gain;
            } else {
                for (std::size_t i = 0; i < n; ++i) master_left[i] += b.left[i] * b.gain, master_right[i] += b.right[i] * b.gain;
            }
        }
        // The limiter: down at once to hold a peak under the ceiling, back up slowly.
        for (std::size_t i = 0; i < n; ++i) {
            const double peak = std::max(std::abs(static_cast<double>(master_left[i])), std::abs(static_cast<double>(master_right[i])));
            const double want = peak > options.ceiling ? options.ceiling / peak : 1.0;  // the most this peak allows
            limit = want < limit ? want : limit + (want - limit) * release;
            if (limit < 0.999) {  // turned down by more than 0.01 dB
                ++limited.frames;
                limited.deepest = std::min(limited.deepest, static_cast<float>(limit));
            }
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
        if (l.wave == Wave::recording) {
            const auto r = std::ranges::find(m.recordings, l.recording, &std::pair<std::string, std::shared_ptr<const Recorded>>::first);
            if (r != m.recordings.end()) {
                q.recorded = r->second;
                q.step = static_cast<double>(r->second->rate) / rate;
                // Length 0: as long as the recording lasts at its speed (the mean of a glide's).
                if (l.length <= 0) {
                    const double speed = dm::sqrt(static_cast<double>(l.speed[0]) * l.speed[1]);
                    q.frames = std::max<std::int64_t>(1, std::llround(static_cast<double>(r->second->samples.size()) / (q.step * speed)));
                }
            }
        }
        const auto glide = [&](float a, float b) { return a > 0 && b > 0 ? dm::exp(dm::log(static_cast<double>(b) / a) / static_cast<double>(q.frames)) : 1.0; };
        q.pitch_step = l.wave == Wave::recording ? glide(l.speed[0], l.speed[1]) : glide(l.pitch[0], l.pitch[1]);
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

void Mixer::add_recording(std::string_view name, std::span<const float> samples, std::int32_t rate) {
    auto r = std::make_shared<Recorded>(Recorded{{samples.begin(), samples.end()}, std::max(1, rate)});
    for (auto& [n, kept] : impl_->recordings)
        if (n == name) {
            kept = std::move(r);
            return;
        }
    impl_->recordings.emplace_back(std::string(name), std::move(r));
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

std::int32_t Mixer::bus(std::string_view name) { return impl_->bus_index(name); }

void Mixer::route(std::string_view bus, std::string_view to) {
    const std::int32_t from = impl_->bus_index(bus), into = impl_->bus_index(to);
    impl_->buses[static_cast<std::size_t>(from)].to = from == into ? -1 : into;
    impl_->reorder();
}

void Mixer::duck(std::string_view bus, const Duck& duck) {
    const std::int32_t b = impl_->bus_index(bus), key = impl_->bus_index(duck.key);
    Bus& d = impl_->buses[static_cast<std::size_t>(b)];
    d.key = key == b ? -1 : key;
    d.duck = duck;
    const double rate = impl_->options.rate;
    d.attack = 1 - dm::exp(-1.0 / std::max(1.0, static_cast<double>(duck.attack) * rate));
    d.release = 1 - dm::exp(-1.0 / std::max(1.0, static_cast<double>(duck.release) * rate));
    impl_->reorder();
}

void Mixer::add_source(std::shared_ptr<Source> source) {
    if (source) impl_->sources.push_back(std::move(source));
}

LimiterStats Mixer::limiter() const noexcept { return impl_->limited; }

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
    hash::Fnv1a h;
    for (const float s : samples) h.le(std::bit_cast<std::uint32_t>(s), 4);
    return h.value();
}

float peak(std::span<const float> samples) noexcept {
    float p = 0;
    for (const float s : samples) p = std::max(p, std::abs(s));
    return p;
}

std::expected<Recording, std::string> read_wav(std::span<const std::byte> file) {
    auto u = [&](std::size_t at, int bytes) {
        std::uint32_t v = 0;
        for (int b = 0; b < bytes; ++b) v |= static_cast<std::uint32_t>(file[at + static_cast<std::size_t>(b)]) << (8 * b);
        return v;
    };
    auto is = [&](std::size_t at, std::string_view tag) {
        if (at + 4 > file.size()) return false;
        for (std::size_t i = 0; i < 4; ++i)
            if (static_cast<char>(file[at + i]) != tag[i]) return false;
        return true;
    };
    if (!is(0, "RIFF") || !is(8, "WAVE")) return std::unexpected(std::string("not a WAV file (no RIFF/WAVE header)"));
    std::uint32_t format = 0, channels = 0, rate = 0, bits = 0;
    std::span<const std::byte> data;
    for (std::size_t at = 12; at + 8 <= file.size();) {  // the chunks: fmt and data, the rest passed over
        const std::size_t size = u(at + 4, 4), body = at + 8;
        if (body + size > file.size()) return std::unexpected(std::format("a chunk runs past the file's end (at byte {})", at));
        if (is(at, "fmt ") && size >= 16) {
            format = u(body, 2), channels = u(body + 2, 2), rate = u(body + 4, 4), bits = u(body + 14, 2);
            if (format == 0xfffe && size >= 26) format = u(body + 24, 2);  // extensible: its sub-format's first two bytes
        } else if (is(at, "data")) {
            data = file.subspan(body, size);
        }
        at = body + size + (size & 1);
    }
    if (channels == 0 || rate == 0) return std::unexpected(std::string("no fmt chunk"));
    if (!((format == 1 && (bits == 8 || bits == 16 || bits == 24)) || (format == 3 && bits == 32)))
        return std::unexpected(std::format("format {} at {} bits: 8-, 16-, 24-bit PCM or 32-bit float is read", format, bits));
    const std::size_t width = bits / 8, frame = width * channels, frames = data.size() / frame;
    Recording r;
    r.rate = static_cast<std::int32_t>(rate);
    r.samples.resize(frames);
    const std::size_t base = static_cast<std::size_t>(data.data() - file.data());
    for (std::size_t i = 0; i < frames; ++i) {
        double sum = 0;
        for (std::size_t c = 0; c < channels; ++c) {
            const std::size_t at = base + i * frame + c * width;
            if (format == 3) sum += std::bit_cast<float>(u(at, 4));
            else if (bits == 8) sum += (static_cast<double>(u(at, 1)) - 128) / 128;
            else if (bits == 16) sum += static_cast<std::int16_t>(u(at, 2)) / 32768.0;
            else sum += static_cast<double>(static_cast<std::int32_t>(u(at, 3) << 8) >> 8) / 8388608.0;
        }
        r.samples[i] = static_cast<float>(sum / channels);
    }
    return r;
}

std::vector<std::byte> wav(std::span<const float> interleaved, std::int32_t rate, bool float32, std::int32_t channels) {
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
    const auto n = static_cast<std::uint32_t>(std::max(1, channels));
    put(float32 ? 3 : 1, 2);  // IEEE float, or PCM
    put(n, 2);
    put(static_cast<std::uint32_t>(rate), 4);
    put(static_cast<std::uint32_t>(rate) * n * bytes_per, 4);
    put(n * bytes_per, 2);
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
