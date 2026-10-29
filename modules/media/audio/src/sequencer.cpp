// The sequencer: a piece compiled once into each section's notes in ticks,
// then played as a source in the mixer. Ticks become samples by an exact
// fraction (a bar starts on the sample its tick lands on, however long the
// piece has looped: nothing drifts); each bar takes the intensity asked for
// when it begins and plays the layers whose range holds it; a note starts on
// its very sample in its instrument's voices, and the instruments add into
// the piece's buses and their sends. All of it is made when the piece is
// attached; nothing is allocated while it plays.
module openmui.audio;

import std;
import hanim;
import openmui.trace;
import :dsp;
import :detail;
import :synth;

namespace omui::audio {

namespace {

namespace dm = hanim::dm;

struct Event {
    std::int64_t tick = 0;       // in its section
    std::int64_t length = 0;     // ticks
    std::int32_t player = 0;
    std::int32_t layer = 0;
    double       freq = 0, to = 0;  // Hz (0: the instrument's own; no slide)
    float        velocity = 0.8f;
    std::int32_t order = 0;      // the order it was written in, for ties
};

struct Compiled {
    std::int32_t                      bars = 1;
    std::vector<Event>                events;  // by tick
    std::vector<std::size_t>          first;   // each bar's first event, and one past the last
    std::vector<std::array<float, 2>> ranges;  // each layer's intensity
};

// An instrument's voices and where they go.
struct Player {
    std::int32_t                    tone = 0;
    std::vector<synth::Voice>       voices;
    std::vector<float>              lines;  // its strings', a voice's after another's
    std::array<float, block_frames> left{}, right{};
    float                           gain = 1, pan_left = 1, pan_right = 1, legato = 0.9f, humanize = 0;
    double                          pitch = 1;  // its transposition, for a drum's own pitch
    std::int32_t                    bus = 0;
    std::vector<std::pair<std::int32_t, float>> sends;
    std::vector<bool>               gate;   // its rhythm (none: open)
    double                          gate_level = 1;
    std::vector<std::int32_t>       chokes;  // the others of its group
    bool                            sounding = false;
};

// A note's own number: where it is in the piece, so that it is the same
// whatever else plays (a layer coming in leaves the others' noise as it was).
std::uint64_t mixed(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

std::int64_t floor_div(std::int64_t a, std::int64_t b) noexcept {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

}  // namespace

struct Sequencer::Impl {
    Music                     music;
    std::int32_t              rate = 48000;
    std::vector<synth::Tone>  tones;
    std::vector<Player>       players;
    std::vector<Compiled>     sections;
    std::vector<std::int32_t> intro, loop, outro;
    std::int64_t              bar_ticks = 1;
    std::int64_t              num = 1, den = 1;  // samples a tick, exactly
    std::int64_t              gate_step = 240;   // a gate's step: a sixteenth
    double                    gate_k = 0.01;
    std::uint64_t             serial = 0;

    // Asked for from any thread.
    std::atomic<std::int64_t>  start_at{0};
    std::atomic<std::uint32_t> start_asked{0}, finish_asked{0};
    std::atomic<float>         want_intensity{1}, want_gain{1};
    std::unique_ptr<std::atomic<bool>[]> muted;
    // Told to any thread.
    std::atomic<std::int64_t>  begun_bars{0}, started{0};
    std::atomic<bool>          is_playing{false};
    std::atomic<std::int32_t>  voices_now{0};

    // The thread that renders.
    std::uint32_t start_seen = 0, finish_seen = 0;
    bool          playing = false, finishing = false, in_bar = false;
    std::int32_t  part = 0;  // 0 intro, 1 loop, 2 outro
    std::size_t   item = 0;  // in that part's list
    std::int32_t  bar = 0;   // in the section
    std::int64_t  origin = 0, section_tick = 0;
    std::size_t   next = 0;  // the next event of the bar
    std::uint64_t mask = 0;  // the bar's layers
    float         gain = 1;

    [[nodiscard]] std::int64_t sample(std::int64_t tick) const noexcept {
        // origin + floor(tick × num / den), split so that it cannot overflow.
        const std::int64_t q = floor_div(tick, den), r = tick - q * den;
        return origin + q * num + r * num / den;
    }

    [[nodiscard]] const std::vector<std::int32_t>& list() const noexcept { return part == 0 ? intro : part == 1 ? loop : outro; }

    // On to the next section; false at the end of the piece.
    bool advance() noexcept {
        section_tick += static_cast<std::int64_t>(sections[static_cast<std::size_t>(list()[item])].bars) * bar_ticks;
        bar = 0;
        if (++item < list().size()) return true;
        item = 0;
        if (part == 0) part = 1;
        else if (part == 2) return false;
        return !loop.empty();
    }

    void note(const Event& e, std::int64_t at, std::int64_t clock) {
        Player& p = players[static_cast<std::size_t>(e.player)];
        const synth::Tone& tone = tones[static_cast<std::size_t>(p.tone)];
        for (const std::int32_t other : p.chokes)
            for (synth::Voice& v : players[static_cast<std::size_t>(other)].voices) synth::choke(v, tones[static_cast<std::size_t>(players[static_cast<std::size_t>(other)].tone)]);
        // A free voice, else the oldest played again.
        std::size_t slot = p.voices.size();
        for (std::size_t v = 0; v < p.voices.size() && slot == p.voices.size(); ++v)
            if (!p.voices[v].active) slot = v;
        if (slot == p.voices.size()) {
            slot = 0;
            for (std::size_t v = 1; v < p.voices.size(); ++v)
                if (p.voices[v].serial < p.voices[slot].serial) slot = v;
        }
        const std::int64_t end = sample(section_tick + e.tick + e.length);
        const auto held = std::max<std::int64_t>(1, std::lround(static_cast<double>(end - at) * p.legato));
        const std::uint64_t id = mixed((static_cast<std::uint64_t>(section_tick) << 20) ^ (static_cast<std::uint64_t>(e.order) << 6) ^ static_cast<std::uint64_t>(e.player));
        // A player's touch: a little softer and later, by the note's number (its place in the piece: the same in every render).
        double velocity = e.velocity;
        std::int64_t start = std::max(at, clock);
        if (p.humanize > 0) {
            const double u = static_cast<double>(id >> 40) / 16777216.0, w = static_cast<double>((id >> 16) & 0xffffff) / 16777216.0;
            velocity *= 1 - 0.3 * p.humanize * u;
            start += std::lround(0.006 * p.humanize * w * rate);
        }
        synth::begin(p.voices[slot], tone, e.freq, e.to, p.pitch, velocity, start, held, ++serial, static_cast<std::uint32_t>(id));
    }

    // The notes that begin in [clock, to).
    void schedule(std::int64_t clock, std::int64_t to) {
        while (playing) {
            const Compiled& s = sections[static_cast<std::size_t>(list()[item])];
            if (!in_bar) {
                const std::int64_t tick = section_tick + bar * bar_ticks;
                if (sample(tick) >= to) return;
                if (finishing && part != 2) {
                    // To the outro on this bar line (or the end).
                    finishing = false;
                    if (outro.empty()) {
                        playing = false;
                        return;
                    }
                    part = 2, item = 0, bar = 0, section_tick = tick;
                    continue;
                }
                const float x = want_intensity.load(std::memory_order_relaxed);
                mask = 0;
                for (std::size_t l = 0; l < s.ranges.size(); ++l) {
                    const auto [lo, hi] = s.ranges[l];
                    if (x >= lo && (x < hi || (hi >= 1 && x >= 1))) mask |= std::uint64_t{1} << l;
                }
                in_bar = true;
                next = s.first[static_cast<std::size_t>(bar)];
                begun_bars.fetch_add(1, std::memory_order_relaxed);
                omui::trace::mark("audio.music.bar", "audio");
            }
            for (const std::size_t last = s.first[static_cast<std::size_t>(bar) + 1]; next < last; ++next) {
                const Event& e = s.events[next];
                const std::int64_t at = sample(section_tick + e.tick);
                if (at >= to) return;
                if ((mask >> e.layer & 1) != 0 && !muted[static_cast<std::size_t>(e.player)].load(std::memory_order_relaxed)) note(e, at, clock);
            }
            in_bar = false;
            if (++bar == s.bars && !advance()) playing = false;
        }
    }

    void render(std::int64_t clock, std::int32_t frames, std::span<const BusBlock> buses) {
        if (const std::uint32_t asked = start_asked.load(std::memory_order_acquire); asked != start_seen) {
            start_seen = asked;
            origin = start_at.load(std::memory_order_relaxed);
            started.store(origin, std::memory_order_relaxed);
            playing = !(intro.empty() && loop.empty());
            part = intro.empty() ? 1 : 0, item = 0, bar = 0, section_tick = 0, in_bar = false, finishing = false;
            finish_seen = finish_asked.load(std::memory_order_relaxed);
            begun_bars.store(0, std::memory_order_relaxed);
        }
        if (const std::uint32_t asked = finish_asked.load(std::memory_order_acquire); asked != finish_seen) finish_seen = asked, finishing = playing;
        schedule(clock, clock + frames);
        is_playing.store(playing, std::memory_order_relaxed);

        const float g0 = gain, g1 = want_gain.load(std::memory_order_relaxed);
        gain = g1;
        const auto n = static_cast<std::size_t>(frames);
        std::int32_t count = 0;
        for (Player& p : players) {
            bool any = false;
            for (const synth::Voice& v : p.voices) any = any || v.active;
            if (!any && !p.sounding) continue;
            p.sounding = any;
            std::fill_n(p.left.begin(), n, 0.0f);
            std::fill_n(p.right.begin(), n, 0.0f);
            const synth::Tone& tone = tones[static_cast<std::size_t>(p.tone)];
            for (synth::Voice& v : p.voices)
                if (v.active) synth::sound(v, tone, clock, frames, p.left.data(), p.right.data()), count += v.active;
            // Its rhythm: open or shut by the sixteenth of the bar it is in, eased over a couple of milliseconds.
            if (!p.gate.empty()) {
                const auto steps = static_cast<std::int64_t>(p.gate.size());
                for (std::size_t i = 0; i < n; ++i) {
                    const std::int64_t s = clock + static_cast<std::int64_t>(i) - origin;
                    const std::int64_t q = floor_div(s, num), tick = q * den + (s - q * num) * den / num;
                    const std::int64_t place = tick - floor_div(tick, bar_ticks) * bar_ticks;
                    const double want = p.gate[static_cast<std::size_t>((place / gate_step) % steps)] ? 1.0 : 0.0;
                    p.gate_level += (want - p.gate_level) * gate_k;
                    p.left[i] = static_cast<float>(p.left[i] * p.gate_level);
                    p.right[i] = static_cast<float>(p.right[i] * p.gate_level);
                }
            }
            const BusBlock& out = buses[static_cast<std::size_t>(p.bus)];
            for (std::size_t i = 0; i < n; ++i) {
                const float g = (g0 + (g1 - g0) * static_cast<float>(i + 1) / static_cast<float>(n)) * p.gain;
                const float l = p.left[i] * g * p.pan_left, r = p.right[i] * g * p.pan_right;
                out.left[i] += l;
                out.right[i] += r;
                for (const auto& [bus, level] : p.sends) {
                    const BusBlock& send = buses[static_cast<std::size_t>(bus)];
                    send.left[i] += l * level;
                    send.right[i] += r * level;
                }
            }
        }
        voices_now.store(count, std::memory_order_relaxed);
        omui::trace::count("audio.music.voices", count, "audio");
    }
};

Sequencer::Sequencer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Sequencer::~Sequencer() = default;

std::expected<std::shared_ptr<Sequencer>, std::vector<Problem>> Sequencer::attach(const Music& music, Mixer& mixer) {
    auto m = std::make_unique<Impl>();
    m->music = music;
    m->rate = mixer.rate();
    std::vector<Problem> problems;
    const std::string where = std::format("music `{}`", music.name);
    auto problem = [&](std::string message, std::string hint = {}) { problems.push_back({where, 0, 0, std::move(message), std::move(hint)}); };
    if (music.tempo <= 0 || music.meter[0] < 1 || music.meter[1] < 1 || whole_ticks % music.meter[1] != 0) {
        problem("its tempo or metre cannot be played", "tempo = 120, meter = [4, 4]");
        return std::unexpected(std::move(problems));
    }
    m->bar_ticks = music.bar_ticks();
    // Samples a tick: rate × 60 × 1000 × the metre's note / (whole × tempo × 1000), reduced.
    const auto milli = static_cast<std::int64_t>(std::llround(static_cast<double>(music.tempo) * 1000));
    m->num = static_cast<std::int64_t>(m->rate) * 60000 * music.meter[1];
    m->den = std::int64_t{whole_ticks} * milli;
    const std::int64_t g = std::gcd(m->num, m->den);
    m->num /= g, m->den /= g;
    m->gate_step = whole_ticks / 16;
    m->gate_k = 1 - dm::exp(-1.0 / (0.002 * m->rate));

    // Buses: the piece's own under "music", each of the file's under its `to`.
    const std::string top = std::format("music.{}", music.name);
    auto bus_name = [&](std::string_view b) { return b.empty() ? top : std::format("{}.{}", top, b); };
    auto has_bus = [&](std::string_view b) { return b.empty() || std::ranges::any_of(music.buses, [&](const MusicBus& x) { return x.name == b; }); };
    const std::int32_t top_index = mixer.bus(top);
    mixer.route(top, "music");
    mixer.bus_gain(top, music.gain);
    for (const MusicBus& b : music.buses) {
        mixer.bus(bus_name(b.name));
        if (!has_bus(b.to)) problem(std::format("bus `{}` goes to `{}`, not one of the piece's", b.name, b.to));
    }
    for (const MusicBus& b : music.buses) {
        mixer.route(bus_name(b.name), bus_name(has_bus(b.to) ? b.to : ""));
        mixer.bus_gain(bus_name(b.name), b.gain);
        for (const EffectSpec& e : b.effects) mixer.add_effect(bus_name(b.name), effect(e, m->rate, music.beat_seconds()));
        if (b.duck) {
            if (!has_bus(b.duck->key)) problem(std::format("bus `{}` ducks under `{}`, not one of the piece's", b.name, b.duck->key));
            Duck d = *b.duck;
            d.key = bus_name(d.key);
            mixer.duck(bus_name(b.name), d);
        }
    }
    (void)top_index;

    // Patches: the piece's, then the built-ins.
    auto patch_of = [&](std::string_view name) -> const Patch* {
        if (const auto it = std::ranges::find(music.patches, name, &Patch::name); it != music.patches.end()) return &*it;
        const auto all = builtin_patches();
        if (const auto it = std::ranges::find(all, name, &Patch::name); it != all.end()) return &*it;
        return nullptr;
    };
    for (const Instrument& in : music.instruments) {
        const Patch* patch = patch_of(in.patch);
        if (!patch) {
            problem(std::format("instrument `{}` plays patch `{}`, which is neither the piece's nor a built-in", in.name, in.patch));
            continue;
        }
        Player p;
        p.tone = static_cast<std::int32_t>(m->tones.size());
        m->tones.push_back(synth::prepare(*patch, m->rate));
        p.voices.resize(static_cast<std::size_t>(std::clamp(in.voices, 1, 64)));
        p.gain = in.gain;
        const double angle = (std::clamp(static_cast<double>(in.pan), -1.0, 1.0) + 1) * dm::pi / 4;
        p.pan_left = static_cast<float>(dm::cos(angle) * 1.4142135623730951);
        p.pan_right = static_cast<float>(dm::sin(angle) * 1.4142135623730951);
        p.legato = std::clamp(in.legato, 0.01f, 1.0f);
        p.humanize = std::clamp(in.humanize, 0.0f, 1.0f);
        p.pitch = dsp::exp2(in.transpose / 12.0);
        if (!has_bus(in.bus)) problem(std::format("instrument `{}` plays into bus `{}`, not one of the piece's", in.name, in.bus));
        p.bus = mixer.bus(bus_name(has_bus(in.bus) ? in.bus : ""));
        for (const Send& s : in.sends) {
            if (!has_bus(s.bus) || s.bus.empty()) problem(std::format("instrument `{}` sends to `{}`, not one of the piece's buses", in.name, s.bus));
            else p.sends.emplace_back(mixer.bus(bus_name(s.bus)), s.level);
        }
        for (const char c : in.gate)
            if (c == 'x' || c == '.') p.gate.push_back(c == 'x');
        m->players.push_back(std::move(p));
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    for (std::size_t a = 0; a < music.instruments.size(); ++a)
        for (std::size_t b = 0; b < music.instruments.size(); ++b)
            if (a != b && !music.instruments[a].choke.empty() && music.instruments[a].choke == music.instruments[b].choke)
                m->players[a].chokes.push_back(static_cast<std::int32_t>(b));
    auto player_of = [&](std::string_view name) -> std::int32_t {
        const auto it = std::ranges::find(music.instruments, name, &Instrument::name);
        return it == music.instruments.end() ? -1 : static_cast<std::int32_t>(it - music.instruments.begin());
    };

    // Sections: each layer's pattern laid over its bars.
    std::vector<double> lowest(music.instruments.size(), 1e9);
    for (const Section& sec : music.sections) {
        Compiled c;
        c.bars = std::max(1, sec.bars);
        const std::int64_t end = c.bars * m->bar_ticks;
        std::int32_t order = 0;
        if (sec.layers.size() > 64) problem(std::format("section `{}` has {} layers; 64 at most", sec.name, sec.layers.size()));
        for (std::size_t li = 0; li < sec.layers.size() && li < 64; ++li) {
            const MusicLayer& layer = sec.layers[li];
            c.ranges.push_back(layer.intensity);
            const auto pat = std::ranges::find(music.patterns, layer.pattern, &Pattern::name);
            if (pat == music.patterns.end()) {
                problem(std::format("section `{}` plays pattern `{}`, which is not in the piece", sec.name, layer.pattern));
                continue;
            }
            auto h = hits(*pat, music);
            if (!h) {
                for (auto& p : h.error()) problems.push_back(std::move(p));
                continue;
            }
            if (h->bars < 1) continue;
            std::vector<std::int32_t> who;  // the hits' players: the notes', then each lane's
            const std::string& by = layer.instrument.empty() ? pat->instrument : layer.instrument;
            const std::int32_t notes = pat->notes.empty() ? -1 : player_of(by);
            if (!pat->notes.empty() && notes < 0) problem(std::format("pattern `{}` is played by `{}`, which is not an instrument", pat->name, by));
            for (const Lane& lane : pat->lanes) {
                who.push_back(player_of(lane.instrument));
                if (who.back() < 0) problem(std::format("pattern `{}` has a lane for `{}`, which is not an instrument", pat->name, lane.instrument));
            }
            const std::int32_t times = layer.times > 0 ? layer.times : (c.bars - layer.at + h->bars - 1) / h->bars;
            for (std::int32_t t = 0; t < times; ++t) {
                const std::int64_t base = static_cast<std::int64_t>(layer.at + t * h->bars) * m->bar_ticks;
                if (base >= end) break;
                for (const Hit& hit : h->hits) {
                    const std::int64_t tick = base + hit.tick;
                    if (tick >= end) continue;
                    const std::int32_t player = hit.lane < 0 ? notes : who[static_cast<std::size_t>(hit.lane)];
                    if (player < 0) continue;
                    const std::int32_t shift = layer.transpose + music.instruments[static_cast<std::size_t>(player)].transpose;
                    Event e{tick, hit.length, player, static_cast<std::int32_t>(li), 0, 0, hit.velocity * layer.gain, order++};
                    if (hit.key >= 0) e.freq = dsp::hz(hit.key + shift);
                    if (hit.to >= 0) e.to = dsp::hz(hit.to + shift);
                    double& low = lowest[static_cast<std::size_t>(player)];
                    low = std::min({low, e.freq > 0 ? e.freq : 1e9, e.to > 0 ? e.to : 1e9});
                    c.events.push_back(e);
                }
            }
        }
        std::ranges::sort(c.events, [](const Event& a, const Event& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
        for (std::int32_t b = 0, at = 0; b <= c.bars; ++b) {
            while (static_cast<std::size_t>(at) < c.events.size() && c.events[static_cast<std::size_t>(at)].tick < b * m->bar_ticks) ++at;
            c.first.push_back(static_cast<std::size_t>(at));
        }
        m->sections.push_back(std::move(c));
    }
    auto section_list = [&](const std::vector<std::string>& names, std::vector<std::int32_t>& out, std::string_view part) {
        for (const std::string& n : names) {
            const auto it = std::ranges::find(music.sections, n, &Section::name);
            if (it == music.sections.end()) problem(std::format("the {} plays section `{}`, which is not in the piece", part, n));
            else out.push_back(static_cast<std::int32_t>(it - music.sections.begin()));
        }
    };
    section_list(music.arrangement.intro, m->intro, "intro");
    section_list(music.arrangement.loop, m->loop, "loop");
    section_list(music.arrangement.outro, m->outro, "outro");
    if (!problems.empty()) return std::unexpected(std::move(problems));

    // Strings long enough for their lowest note (a little lower: a vibrato's dip).
    for (std::size_t i = 0; i < m->players.size(); ++i) {
        Player& p = m->players[i];
        if (m->tones[static_cast<std::size_t>(p.tone)].patch.engine != Engine::pluck) continue;
        const double low = lowest[i] < 1e9 ? lowest[i] * 0.9 : 55.0;
        const std::size_t size = synth::line_size(low, m->rate);
        p.lines.assign(size * p.voices.size(), 0.0f);
        for (std::size_t v = 0; v < p.voices.size(); ++v) p.voices[v].line = std::span(p.lines).subspan(v * size, size);
    }
    m->muted = std::make_unique<std::atomic<bool>[]>(m->players.size());
    for (std::size_t i = 0; i < m->players.size(); ++i) m->muted[i].store(false);
    std::shared_ptr<Sequencer> s(new Sequencer(std::move(m)));
    mixer.add_source(s);
    return s;
}

void Sequencer::start(std::int64_t at) noexcept {
    impl_->start_at.store(at, std::memory_order_relaxed);
    impl_->start_asked.fetch_add(1, std::memory_order_release);
}
void Sequencer::finish() noexcept { impl_->finish_asked.fetch_add(1, std::memory_order_release); }
void Sequencer::set_intensity(float intensity) noexcept { impl_->want_intensity.store(std::clamp(intensity, 0.0f, 1.0f), std::memory_order_relaxed); }
void Sequencer::set_gain(float gain) noexcept { impl_->want_gain.store(std::max(0.0f, gain), std::memory_order_relaxed); }
void Sequencer::mute(std::string_view instrument, bool muted) {
    for (std::size_t i = 0; i < impl_->music.instruments.size(); ++i)
        if (impl_->music.instruments[i].name == instrument) impl_->muted[i].store(muted);
}
float Sequencer::intensity() const noexcept { return impl_->want_intensity.load(std::memory_order_relaxed); }
std::int64_t Sequencer::bars() const noexcept { return impl_->begun_bars.load(std::memory_order_relaxed); }
bool Sequencer::playing() const noexcept { return impl_->is_playing.load(std::memory_order_relaxed); }
std::int32_t Sequencer::sounding() const noexcept { return impl_->voices_now.load(std::memory_order_relaxed); }
const Music& Sequencer::music() const noexcept { return impl_->music; }

std::int64_t Sequencer::bar_sample(std::int64_t bar) const noexcept {
    const Impl& m = *impl_;
    const std::int64_t tick = bar * m.bar_ticks;
    const std::int64_t q = floor_div(tick, m.den), r = tick - q * m.den;
    return m.start_at.load(std::memory_order_relaxed) + q * m.num + r * m.num / m.den;
}

void Sequencer::render(std::int64_t clock, std::int32_t frames, std::span<const BusBlock> buses) noexcept {
    omui::trace::Span span("audio.music", "audio");
    impl_->render(clock, frames, buses);
}

std::expected<MusicRender, std::vector<Problem>> render_music(const Music& music, double seconds, std::span<const Cue> cues, std::span<const std::string> solo,
                                                              Options options) {
    Mixer mixer(options);
    auto s = Sequencer::attach(music, mixer);
    if (!s) return std::unexpected(std::move(s.error()));
    Sequencer& seq = **s;
    if (!solo.empty()) {
        std::vector<Problem> problems;
        for (const std::string& name : solo)
            if (std::ranges::find(music.instruments, name, &Instrument::name) == music.instruments.end())
                problems.push_back({std::format("music `{}`", music.name), 0, 0, std::format("`{}` is not one of its instruments", name), {}});
        if (!problems.empty()) return std::unexpected(std::move(problems));
        for (const Instrument& in : music.instruments) seq.mute(in.name, std::ranges::find(solo, in.name) == solo.end());
    }
    std::vector<Cue> sorted(cues.begin(), cues.end());
    std::ranges::stable_sort(sorted, {}, &Cue::at);
    const auto total = std::max<std::int64_t>(0, std::llround(seconds * mixer.rate()));
    MusicRender out;
    out.samples.resize(static_cast<std::size_t>(2 * total));
    std::int64_t done = 0;
    std::size_t next = 0;
    seq.start(0);
    while (done < total) {
        while (next < sorted.size() && std::llround(sorted[next].at * mixer.rate()) <= done) seq.set_intensity(sorted[next++].intensity);
        std::int64_t until = total;
        if (next < sorted.size()) until = std::clamp<std::int64_t>(std::llround(sorted[next].at * mixer.rate()), done + 1, total);
        mixer.render(std::span(out.samples).subspan(static_cast<std::size_t>(2 * done), static_cast<std::size_t>(2 * (until - done))));
        done = until;
    }
    out.limiter = mixer.limiter();
    return out;
}

}  // namespace omui::audio
