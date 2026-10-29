// Music as data and its sequencer: a piece read and written back, its
// problems at their line and column (a wrong note at its very token); the
// instruments in tune and shaped (a string's pitch, a drum's fall, unison's
// width, a slide reaching its note); the effects (a reverb's tail, a chorus,
// ducking under a key); notes on their very samples; the intensity changing
// what plays from the next bar; the loop going round without a click;
// nothing allocated while playing; traced; and a piece rendered offline, its
// bits pinned on every platform.
import std;
import openmui.audio;
import openmui.testing;
import openmui.trace;

using namespace omui::testing;
namespace audio = omui::audio;

// Heap allocations counted here, in the test program only.
namespace {
std::atomic<std::int64_t> allocations{0};
}
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

// A small piece with something of everything: a pluck lead with a slide and a
// chord, an FM bell, a pad pumping under its kick, drum lanes with a choke,
// a gated pad, a reverb and a ping-pong echo, an intro, a loop of two
// sections with layers by intensity, and an outro.
constexpr std::string_view piece = R"(doc = """A test piece: every part of the format once."""

[music]
name = "test"
tempo = 140
meter = [4, 4]
gain = 0.4

[patch.lead]
base = "koto"
pluck = { decay = 1.5 }
gain = 0.9

[patch.warm]
engine = "analog"
osc = { wave = "saw", unison = 3, detune = 15, spread = 0.7 }
amp = { attack = 0.05, decay = 0.5, sustain = 0.8, release = 0.4 }
filter = { kind = "lowpass", cutoff = 1200, q = 0.7, envelope = 1, attack = 0.1, decay = 0.5, sustain = 0.3, release = 0.4 }

[instrument.lead]
patch = "lead"
gain = 0.7
pan = -0.2
sends = { echo = 0.2, room = 0.3 }

[instrument.bell]
patch = "bell"
gain = 0.3
pan = 0.3
sends = { room = 0.4 }

[instrument.pad]
patch = "warm"
bus = "pads"
gain = 0.35
voices = 6
sends = { room = 0.3 }

[instrument.gated]
patch = "warm"
gain = 0.2
gate = "x.x.xx.x x.x.xx.x"

[instrument.kick]
patch = "kick"
bus = "kick"
gain = 0.8

[instrument.hat]
patch = "hat"
gain = 0.25
humanize = 0.4
choke = "hats"

[instrument.open]
patch = "open_hat"
gain = 0.2
choke = "hats"

[bus.room]
effects = [{ kind = "reverb", size = 0.8, wet = 1, dry = 0 }, { kind = "highpass", cutoff = 200 }]

[bus.echo]
effects = [{ kind = "echo", beats = 0.75, feedback = 0.35, pingpong = true, wet = 1, dry = 0 }]

[bus.pads]
effects = [{ kind = "chorus", rate = 0.4 }]
duck = { key = "kick", depth = 0.6, release = 0.15 }

[bus.kick]

[pattern.hook]
instrument = "lead"
notes = """
D5:2 . F5 G5:2 A5>C6:2 | A5:3 G5 F5:2 D5:2? |
"""

[pattern.chords]
instrument = "pad"
step = "1/1"
notes = "D3+A3+F4 | C3+G3+E4"

[pattern.ring]
instrument = "bell"
notes = "D6:8 | A5:8"

[pattern.chop]
instrument = "gated"
step = "1/2"
notes = "D4+F4+A4:2 | C4+E4+G4:2"

[pattern.beat]
step = "1/16"

[pattern.beat.lanes]
kick = "x...x...x...x..."
hat  = "x.x.x.x.x.x.x..."
open = "..............x."

[section.intro]
bars = 2
layers = [{ pattern = "ring" }, { pattern = "chords" }]

[section.a]
bars = 4
layers = [
    { pattern = "hook" },
    { pattern = "chords" },
    { pattern = "beat", intensity = [0.5, 1] },
    { pattern = "chop", intensity = [0.8, 1] },
]

[section.b]
bars = 4
layers = [
    { pattern = "hook", transpose = 5, gain = 0.9 },
    { pattern = "hook", instrument = "bell", transpose = 17, gain = 0.5, intensity = [0.8, 1] },
    { pattern = "ring", at = 2, times = 1 },
    { pattern = "chords", transpose = 5 },
    { pattern = "beat" },
]

[section.end]
bars = 1
layers = [{ pattern = "ring" }]

[arrangement]
intro = ["intro"]
loop = ["a", "b"]
outro = ["end"]
)";

audio::Music read_piece(std::string_view text = piece) {
    auto m = audio::read_music(text, "test.music.omui.toml");
    if (!m) {
        for (const auto& p : m.error()) fail(p.text());
        return {};
    }
    return *m;
}

std::vector<std::string> problems_of(std::string_view text) {
    std::vector<std::string> v;
    if (auto m = audio::read_music(text, "bad.music.omui.toml"); !m)
        for (const auto& p : m.error()) v.push_back(p.text());
    return v;
}

// A one-instrument piece around `notes` (`patch`, `tempo`).
std::string solo_piece(std::string_view patch, std::string_view notes, std::string_view extra = {}, int tempo = 120) {
    return std::format(R"([music]
name = "solo"
tempo = {}
meter = [4, 4]

[instrument.x]
patch = "{}"
{}
[pattern.p]
instrument = "x"
notes = "{}"

[section.s]
bars = 1
layers = [{{ pattern = "p" }}]

[arrangement]
loop = ["s"]
)",
                       tempo, patch, extra, notes);
}

std::vector<float> render(const audio::Music& m, double seconds, std::span<const audio::Cue> cues = {}) {
    auto r = audio::render_music(m, seconds, cues);
    if (!r) {
        for (const auto& p : r.error()) fail(p.text());
        return {};
    }
    return std::move(r->samples);
}

// The fundamental of the left channel over [from, to) frames by autocorrelation (Hz).
double pitch(std::span<const float> lr, std::size_t from, std::size_t to, double lo = 50, double hi = 2000) {
    std::vector<double> x;
    for (std::size_t i = from; i < to; ++i) x.push_back(lr[2 * i]);
    const auto lag_lo = static_cast<std::size_t>(48000 / hi), lag_hi = static_cast<std::size_t>(48000 / lo);
    double best = -1;
    std::size_t at = lag_lo;
    std::vector<double> r(lag_hi + 2, 0);
    for (std::size_t lag = lag_lo; lag <= lag_hi + 1 && lag < x.size(); ++lag) {
        double s = 0, e = 0;
        for (std::size_t i = 0; i + lag < x.size(); ++i) s += x[i] * x[i + lag], e += x[i] * x[i];
        r[lag] = e > 0 ? s / e : 0;
    }
    // The first peak near the highest (octave errors avoided by the first that comes close).
    for (std::size_t lag = lag_lo; lag <= lag_hi; ++lag) best = std::max(best, r[lag]);
    for (std::size_t lag = lag_lo + 1; lag <= lag_hi; ++lag)
        if (r[lag] >= 0.9 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
            at = lag;
            break;
        }
    // Parabolic interpolation around the peak.
    const double a = r[at - 1], b = r[at], c = r[at + 1];
    const double shift = (a - 2 * b + c) != 0 ? 0.5 * (a - c) / (a - 2 * b + c) : 0;
    return 48000 / (static_cast<double>(at) + shift);
}

double rms(std::span<const float> lr, std::size_t from, std::size_t to) {
    double s = 0;
    for (std::size_t i = from; i < to; ++i) s += static_cast<double>(lr[2 * i]) * lr[2 * i] + static_cast<double>(lr[2 * i + 1]) * lr[2 * i + 1];
    return std::sqrt(s / static_cast<double>(2 * (to - from)));
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"music"};

    s.test("the built-in patches read; a piece reads, and what is written reads back as itself", [] {
        check(audio::builtin_patches().size() >= 25, std::format("{} built-in patches", audio::builtin_patches().size()));
        const audio::Music m = read_piece();
        check_eq(m.name, std::string("test"));
        check(m.tempo == 140 && m.meter == std::array<std::int32_t, 2>{4, 4} && m.gain == 0.4f);
        check_eq(m.instruments.size(), std::size_t{7});
        check_eq(m.patterns.size(), std::size_t{5});
        check(m.arrangement.intro == std::vector<std::string>{"intro"} && m.arrangement.loop == std::vector<std::string>{"a", "b"});
        check_eq(m.bars(m.arrangement.loop), std::int32_t{8});
        const auto lead = std::ranges::find(m.patches, "lead", &audio::Patch::name);
        const auto koto = std::ranges::find(audio::builtin_patches(), "koto", &audio::Patch::name);
        check(lead != m.patches.end() && lead->base == "koto" && lead->engine == audio::Engine::pluck && lead->pluck.decay == 1.5f &&
                  lead->pluck.brightness == koto->pluck.brightness && lead->amp == koto->amp,
              "a patch on a base: the base's, and what it changes");
        const std::string written = audio::write_music(m);
        auto again = audio::read_music(written, "written.music.omui.toml");
        check(again.has_value(), again ? "" : again.error().front().text());
        if (!again) return;
        check(*again == m, "the same piece");
        check_eq(audio::write_music(*again), written);
    });

    s.test("a pattern's text: notes, lengths, chords, slides, accents and bar lines, into ticks; drum lanes repeated to the longest", [] {
        const audio::Music m = read_piece();
        const auto hook = *std::ranges::find(m.patterns, "hook", &audio::Pattern::name);
        auto h = audio::hits(hook, m);
        check(h.has_value());
        if (!h) return;
        check_eq(h->bars, std::int32_t{2});
        // D5:2 . F5 G5:2 A5>C6:2 — eighths of 480 ticks.
        check(h->hits[0] == audio::Hit{0, 960, 74, -1, 0.8f, -1}, "D5 for two eighths");
        check(h->hits[1] == audio::Hit{1440, 480, 77, -1, 0.8f, -1}, "F5 after a rest");
        check(h->hits[3] == audio::Hit{2880, 960, 81, 84, 0.8f, -1}, "A5 sliding to C6");
        check(h->hits.back().velocity == 0.5f && h->hits.back().key == 74, "a soft D5 last");
        const auto chords = *std::ranges::find(m.patterns, "chords", &audio::Pattern::name);
        auto c = audio::hits(chords, m);
        check(c && c->hits.size() == 6 && c->hits[3].tick == 3840 && c->hits[0].length == 3840, "three-note chords a bar each");
        const auto beat = *std::ranges::find(m.patterns, "beat", &audio::Pattern::name);
        auto b = audio::hits(beat, m);
        const auto kick = static_cast<std::int32_t>(std::ranges::find(beat.lanes, "kick", &audio::Lane::instrument) - beat.lanes.begin());
        check(b && b->bars == 1 && std::ranges::count(b->hits, kick, &audio::Hit::lane) == 4, "four kicks in the bar");
    });

    s.test("every problem at its line and column, with the nearest right word; a wrong note at its very token", [] {
        const auto bad = problems_of(R"([music]
name = "bad"
tempp = 120

[instrument.lead]
patch = "kotto"
sends = { rooom = 0.3 }

[bus.room]
effects = [{ kind = "reverb", sise = 0.8 }]

[pattern.hook]
instrument = "leed"
notes = "D5 F5 H5 G5 A5 C6 D6 E6 | D5:9"

[pattern.multi]
instrument = "lead"
notes = """
D5 F5 G5 A5 C6 D6 E6 G6 |
D5 F5 G5 A5 C6 D6 E6 Q6 |
"""

[pattern.drums]
[pattern.drums.lanes]
lead = "x...x...x..z...."

[section.a]
bars = 4
layers = [{ pattern = "hok", intensity = [0.8, 0.2] }]

[arrangement]
loop = ["b"]
)");
        auto has = [&](std::string_view t) { return std::ranges::any_of(bad, [&](const std::string& x) { return x == t; }); };
        const std::string all = std::format("{}", bad);
        check(has("bad.music.omui.toml:3:1: [music] has no key `tempp` (did you mean `tempo`?)"), all);
        check(has("bad.music.omui.toml:6:9: instrument `lead`'s patch `kotto` is not a patch of the piece's or a built-in (did you mean `koto`?)"), all);
        check(has("bad.music.omui.toml:7:11: instrument `lead` sends to `rooom` is not one of the piece's buses (did you mean `room`?)"), all);
        check(has("bad.music.omui.toml:10:31: a reverb has no key `sise` (did you mean `size`?)"), all);
        check(has("bad.music.omui.toml:13:14: pattern `hook`'s instrument `leed` is not one of the piece's instruments (did you mean `lead`?)"), all);
        check(has("bad.music.omui.toml:14:16: `H5` is not a note (a note is a letter A–G, maybe # or b, and an octave: `C4`, `F#3`, `Bb5`; `.` is a rest)"), all);
        check(has("bad.music.omui.toml:14:40: the last stretch is 9 steps, not whole bars (a bar is 8 1/8 steps)"), all);
        check(has("bad.music.omui.toml:20:22: `Q6` is not a note (a note is a letter A–G, maybe # or b, and an octave: `C4`, `F#3`, `Bb5`; `.` is a rest)"), all);
        check(has("bad.music.omui.toml:25:20: `z` is not a step (`x` a hit, `X` an accent, `o` a ghost, `.` none, `|` a bar line)"), all);
        check(has("bad.music.omui.toml:29:42: an intensity's range [0.8, 0.2] holds nothing (from below to: [0.5, 1])"), all);
        check(has("bad.music.omui.toml:29:23: section `a` plays `hok` is not one of the piece's patterns (did you mean `hook`?)"), all);
        check(has("bad.music.omui.toml:32:9: the loop plays `b` is not one of the piece's sections (one of `a`)"), all);
        // Problems come in the file's order.
        check(std::ranges::is_sorted(bad, {}, [](const std::string& t) {
                  const auto a = t.find(':') + 1, b = t.find(':', a);
                  return std::stoi(t.substr(a, b - a));
              }),
              all);
        const auto malformed = problems_of("[music]\ntempo = \n");
        check(malformed.size() == 1 && malformed[0].starts_with("bad.music.omui.toml:2:"), std::format("{}", malformed));
    });

    s.test("a plucked string plays in tune (from low to high) and rings down; a slide reaches its note", [] {
        for (const auto& [note, hz] : std::array<std::pair<std::string_view, double>, 6>{{{"A2", 110}, {"E3", 164.81}, {"A4", 440}, {"E5", 659.26}, {"D6", 1174.66}, {"G6", 1567.98}}}) {
            const auto m = read_piece(solo_piece("koto", std::format("{}:8", note)));
            const auto out = render(m, 1.0);
            if (out.empty()) return;
            const double f = pitch(out, 4800, 4800 + 9600, hz * 0.7, hz * 1.4);
            std::println("    {} {:.2f} Hz (of {})", note, f, hz);
            check(std::abs(f / hz - 1) < 0.0015, std::format("{}: {} Hz, not {}", note, f, hz));  // under 3 cents
            check(rms(out, 4800, 9600) > 4 * rms(out, 40000, 45000), std::format("{} rings down", note));
        }
        const auto m = read_piece(solo_piece("koto", "A4>C5:8"));
        const auto out = render(m, 1.0);
        const double f = pitch(out, 24000, 33600, 350, 700);
        check(std::abs(f / 523.25 - 1) < 0.006, std::format("slid to {} Hz, not C5's 523.25", f));
    });

    s.test("an FM bell, a saw stack and a pulse play at their pitch; unison spreads across the stereo field, one voice does not", [] {
        for (const auto& [patch, note, hz] : std::array<std::tuple<std::string_view, std::string_view, double>, 4>{
                 {{"epiano", "A3", 220}, {"sub_bass", "A1", 55}, {"pulse_bass", "A2", 110}, {"saw_lead", "A4", 440}}}) {
            const auto m = read_piece(solo_piece(patch, std::format("{}:8", note)));
            const auto out = render(m, 1.0);
            if (out.empty()) return;
            const double f = pitch(out, 9600, 9600 + 9600, hz * 0.7, hz * 1.4);
            check(std::abs(f / hz - 1) < 0.01, std::format("{} {}: {} Hz, not {}", patch, note, f, hz));
        }
        auto correlation = [](std::string_view patch) {
            const auto out = render(read_piece(solo_piece(patch, "A3:8")), 0.6);
            return audio::levels(out, 48000).correlation;
        };
        const double wide = correlation("supersaw"), narrow = correlation("saw_lead"), mono = correlation("epiano");
        check(wide < 0.8 && mono > 0.999 && narrow > wide, std::format("supersaw {}, saw lead {}, e-piano {}", wide, narrow, mono));
    });

    s.test("drums: a kick falls in pitch and dies; a closed hat is short and bright, a taiko long and low; an open hat is choked by a closed one", [] {
        auto hit = [](std::string_view patch) {  // a lane: the drum at its own pitch
            const auto m = read_piece(std::format(R"([music]
name = "drum"
tempo = 120
meter = [4, 4]
[instrument.x]
patch = "{}"
[pattern.p]
[pattern.p.lanes]
x = "x..............."
[section.s]
bars = 1
layers = [{{ pattern = "p" }}]
[arrangement]
loop = ["s"]
)",
                                                  patch));
            return render(m, 1.0);
        };
        const auto kick = hit("kick"), hat = hit("hat"), taiko = hit("taiko");
        if (kick.empty() || hat.empty() || taiko.empty()) return;
        // Upward zero crossings over 40 ms: many at the hit, about two (48 Hz) once it has fallen.
        auto crossings = [&](std::size_t from) {
            int n = 0;
            for (std::size_t i = from + 1; i < from + 1920; ++i) n += kick[2 * (i - 1)] < 0 && kick[2 * i] >= 0;
            return n;
        };
        const int early = crossings(240), late = crossings(12000);
        const double settled = pitch(kick, 9600, 19200, 30, 200);
        check(early >= 4 && late <= 3 && settled > 44 && settled < 52, std::format("the kick: {} cycles, then {}; settled at {} Hz", early, late, settled));
        check(rms(kick, 40000, 48000) < 1e-4, "and dies");
        const auto hl = audio::levels(std::span(hat).first(2 * 9600), 48000), tl = audio::levels(std::span(taiko).first(2 * 24000), 48000);
        check(hl.bands[4] > -3 && tl.bands[0] > -6, std::format("the hat's air {} dB, the taiko's sub {} dB", hl.bands[4], tl.bands[0]));
        check(rms(hat, 9600, 14400) < 0.01 * rms(hat, 0, 2400) && rms(taiko, 14400, 19200) > 0.05 * rms(taiko, 0, 2400), "the hat short, the taiko long");
        // An open hat alone rings; followed by a closed one it is cut.
        auto lanes = [](std::string_view open, std::string_view closed) {
            return read_piece(std::format(R"([music]
name = "hats"
tempo = 120
meter = [4, 4]
[instrument.open]
patch = "open_hat"
choke = "hats"
[instrument.closed]
patch = "hat"
gain = 0
choke = "hats"
[pattern.p]
[pattern.p.lanes]
open = "{}"
closed = "{}"
[section.s]
bars = 1
layers = [{{ pattern = "p" }}]
[arrangement]
loop = ["s"]
)",
                                          open, closed));
        };
        const auto alone = render(lanes("x...............", "................"), 0.4);
        const auto choked = render(lanes("x...............", ".x.............."), 0.4);
        check(rms(choked, 9000, 12000) < 0.05 * rms(alone, 9000, 12000), std::format("{} choked, {} alone", rms(choked, 9000, 12000), rms(alone, 9000, 12000)));
    });

    s.test("effects: a reverb's tail falls away; a chorus moves; a ping-pong echo crosses; a bus ducks under its key", [] {
        audio::Mixer m;
        m.add_effect("sfx", audio::effect({.kind = audio::EffectKind::reverb, .wet = 1, .dry = 0, .size = 0.7f}, 48000));
        audio::Sound click{.name = "click"};
        click.layers.push_back({.wave = audio::Wave::noise, .length = 0.005f, .envelope = {.attack = 0.0001f, .decay = 0.004f, .sustain = 0, .release = 0.0005f}});
        m.play(m.add(click), 0);
        const auto tail = audio::render(m, 4 * 48000);
        const double early = rms(tail, 2400, 12000), mid = rms(tail, 48000, 52800), late = rms(tail, 3 * 48000, 4 * 48000 - 1);
        check(early > 0 && mid < early && late < 0.01 * early, std::format("{} then {} then {}", early, mid, late));
        // Ducking: a steady tone on "music" down while a loud one plays on "sfx", back after.
        audio::Mixer d;
        audio::Sound tone{.name = "tone", .bus = "music"};
        tone.layers.push_back({.length = 2, .pitch = {220, 220}, .envelope = {.attack = 0.001f, .hold = 3, .decay = 0.1f, .sustain = 1, .release = 0.01f}, .gain = 0.3f});
        audio::Sound hit{.name = "hit"};
        hit.layers.push_back({.length = 0.3f, .pitch = {880, 880}, .envelope = {.attack = 0.001f, .hold = 1, .decay = 0.1f, .sustain = 1, .release = 0.01f}, .gain = 0.5f});
        d.duck("music", {.key = "sfx", .depth = 0.7f, .threshold = 0.25f, .attack = 0.002f, .release = 0.1f});
        d.bus_gain("sfx", 0);  // the key heard (its level before its gain), but not in the master: what is left is the music
        d.play(d.add(tone), 0);
        d.play(d.add(hit), 24000);
        const auto out = audio::render(d, 96000);
        const double before = rms(out, 12000, 23000), during = rms(out, 30000, 36000), after = rms(out, 70000, 90000);
        check(during < 0.5 * before && after > 0.95 * before, std::format("{} → {} → {}", before, during, after));
        // A chorus and a ping-pong echo through a piece's buses (the test piece's pads and lead).
        const auto piece_out = render(read_piece(), 3.0);
        check(audio::levels(piece_out, 48000).correlation < 0.95, "spread wide");
    });

    s.test("notes start on their very samples: a bar of four beats at 120 is 96000 samples, each hit on its tick's sample", [] {
        const auto m = read_piece(solo_piece("woodblock", "C5 . C5 . C5 . C5 ."));
        const auto out = render(m, 2.2);
        if (out.empty()) return;
        for (std::size_t beat = 0; beat < 5; ++beat) {
            const std::size_t at = beat * 24000;
            bool silent = true;
            for (std::size_t i = at >= 2000 ? at - 2000 : 0; i < at; ++i) silent = silent && out[2 * i] == 0 && out[2 * i + 1] == 0;
            check(silent && out[2 * at] != 0, std::format("silent up to beat {}, sounding on its very sample", beat));
        }
    });

    s.test("intensity: a layer comes in at the next bar after it is asked for, and goes at the next bar after", [] {
        const auto m = read_piece(R"([music]
name = "layers"
tempo = 120
meter = [4, 4]
[instrument.kick]
patch = "woodblock"
[instrument.pad]
patch = "sub_bass"
gain = 0.2
[pattern.k]
[pattern.k.lanes]
kick = "x...x...x...x..."
[pattern.p]
instrument = "pad"
notes = "A2:8"
[section.s]
bars = 4
layers = [{ pattern = "p" }, { pattern = "k", intensity = [0.5, 1] }]
[arrangement]
loop = ["s"]
)");
        // Played twice: once asked for more in the middle of bar 1 and for none early in bar 3, once never.
        // The difference is the layer alone (the rest is the same bits).
        auto play = [&](bool asked, std::int64_t& bar, std::int64_t& bars) {
            audio::Mixer mixer;
            auto seq = audio::Sequencer::attach(m, mixer);
            auto& song = **seq;
            song.set_intensity(0.2f);
            song.start(0);
            bar = song.bar_sample(1);
            auto out = audio::render(mixer, 144000);
            if (asked) song.set_intensity(0.9f);
            auto more = audio::render(mixer, 312000 - 144000);
            if (asked) song.set_intensity(0.0f);
            auto rest = audio::render(mixer, 480000 - 312000);
            out.insert(out.end(), more.begin(), more.end());
            out.insert(out.end(), rest.begin(), rest.end());
            bars = song.bars();
            return out;
        };
        std::int64_t bar = 0, bars = 0;
        const auto with = play(true, bar, bars), without = play(false, bar, bars);
        check_eq(bar, std::int64_t{96000});
        check_eq(bars, std::int64_t{5});
        std::vector<float> layer(with.size());
        for (std::size_t i = 0; i < with.size(); ++i) layer[i] = with[i] - without[i];
        auto silent = [&](std::size_t from, std::size_t to) { return std::all_of(layer.begin() + static_cast<std::ptrdiff_t>(2 * from), layer.begin() + static_cast<std::ptrdiff_t>(2 * to), [](float x) { return x == 0; }); };
        check(silent(0, 192000), "nothing of it before bar 2");
        check(layer[2 * 192000] != 0, "on bar 2's very first sample");
        check(rms(layer, 360000, 362000) > 0.01, "on bar 3's last beat (asked for none after bar 3 began)");
        check(silent(384000, 480000), "nothing of it in bar 4");
    });

    s.test("the loop goes round without a click: what rings at its end rings on into its start, sample to sample", [] {
        const auto m = read_piece();
        // The intro is 2 bars and the loop 8 at 140: the loop's end is at bar 10.
        const auto out = render(m, 22.0);
        if (out.empty()) return;
        audio::Mixer probe;
        auto seq = audio::Sequencer::attach(m, probe);
        const auto loop_point = static_cast<std::size_t>((*seq)->bar_sample(10));
        const auto first = static_cast<std::size_t>((*seq)->bar_sample(2));
        check_eq(loop_point, std::size_t{822857});
        // A tone held to the end of a one-bar loop and struck again as it goes round: the curve of the sound (its second
        // difference) is as smooth across the loop point as in the middle of the bar; no step, no gap.
        const auto tone = read_piece(R"([music]
name = "round"
tempo = 120
meter = [4, 4]
[patch.sine]
engine = "analog"
osc = { wave = "sine" }
amp = { attack = 0.02, decay = 1, sustain = 1, release = 0.3 }
[instrument.x]
patch = "sine"
legato = 1
voices = 2
[pattern.p]
instrument = "x"
notes = "A3:4 C4:4"
[section.s]
bars = 1
layers = [{ pattern = "p" }]
[arrangement]
loop = ["s"]
)");
        const auto round = render(tone, 7.0);
        if (round.empty()) return;
        auto bend = [&](std::size_t from, std::size_t to) {
            double d = 0;
            for (std::size_t i = from; i < to; ++i) d = std::max(d, std::abs(static_cast<double>(round[2 * i]) - 2.0 * round[2 * (i - 1)] + round[2 * (i - 2)]));
            return d;
        };
        for (const std::size_t pass : {96000u, 192000u, 288000u}) {
            const double across = bend(pass - 256, pass + 256), within = bend(pass - 48000 - 256, pass - 48000 + 256);
            check(across < 2 * within && rms(round, pass - 480, pass + 480) > 0.05, std::format("at {}: {} across the loop point, {} within the bar", pass, across, within));
        }
        // Around the loop point the sound is continuous: the pad's chord, held over the bar line, is not cut.
        check(rms(out, loop_point - 2400, loop_point) > 0.01 && rms(out, loop_point, loop_point + 2400) > 0.01, "sounding across it");
    });

    s.test("finishing: the outro from the next bar, then nothing new", [] {
        const auto m = read_piece();
        audio::Mixer mixer;
        auto seq = audio::Sequencer::attach(m, mixer);
        auto& song = **seq;
        song.start(0);
        (void)audio::render(mixer, 48000 * 5);
        song.finish();
        (void)audio::render(mixer, 48000 * 6);
        check(!song.playing(), "stopped after its outro");
        const std::int64_t bars = song.bars();
        (void)audio::render(mixer, 48000 * 4);
        check_eq(song.bars(), bars);
        const auto tail = audio::render(mixer, 48000);
        check(audio::peak(tail) < 1e-3, std::format("silent at last: {}", audio::peak(tail)));
    });

    s.test("nothing is allocated while playing: notes, voices taken again, bars, loops, intensity, a finish", [] {
        const auto m = read_piece();
        audio::Mixer mixer;
        auto seq = audio::Sequencer::attach(m, mixer);
        if (!seq) return;
        auto& song = **seq;
        song.start(100);
        std::vector<float> block(2 * 256);
        mixer.render(block);  // a first block (a trace buffer, if any, made once)
        const std::int64_t before = allocations.load();
        for (int b = 0; b < 4000; ++b) {  // 21 s: round the loop more than once
            if (b % 500 == 0) song.set_intensity(static_cast<float>(b % 1000) / 1000.0f + 0.3f);
            if (b == 3900) song.finish();
            mixer.render(block);
        }
        check_eq(allocations.load() - before, std::int64_t{0});
        check(song.bars() > 10, std::format("{} bars", song.bars()));
    });

    s.test("traced: a span a render and a mark a bar, in category `audio`", [] {
        const auto m = read_piece();
        audio::Mixer mixer;
        auto seq = audio::Sequencer::attach(m, mixer);
        (*seq)->start(0);
        omui::trace::start();
        (void)audio::render(mixer, 48000 * 4);
        const auto t = omui::trace::stop();
        const auto spans = std::ranges::count_if(t.events, [](const auto& e) { return std::string_view(e.name) == "audio.music" && std::string_view(e.category) == "audio"; });
        const auto bars = std::ranges::count_if(t.events, [](const auto& e) { return std::string_view(e.name) == "audio.music.bar"; });
        check(spans > 100 && bars >= 2, std::format("{} span events, {} bars", spans, bars));
    });

    s.test("a piece rendered offline is the same bits everywhere, under the limiter's ceiling without it working hard", [] {
        const auto m = read_piece();
        const std::array<audio::Cue, 3> cues{{{0, 1}, {6, 0.3f}, {12, 1}}};
        auto r = audio::render_music(m, 16.0, cues);
        check(r.has_value());
        if (!r) return;
        const auto l = audio::levels(r->samples, 48000);
        check(l.peak > 0.2f && l.peak <= 0.89f + 1e-6f, std::format("peak {}", l.peak));
        check(r->limiter.deepest > 0.9f, std::format("the limiter down to {}", r->limiter.deepest));
        std::println("    piece {:016x}  peak {:.3f}  {:.1f} LUFS  limiter {:.3f} over {} frames", audio::hash(r->samples), l.peak, l.loudness, r->limiter.deepest,
                     r->limiter.frames);
        const auto w = audio::wav(r->samples, 48000);
        const auto dir = scratch_dir("music");
        std::ofstream(dir / "piece.wav", std::ios::binary).write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
        check_eq(audio::hash(r->samples), std::uint64_t{0xab0350d3813bbcc8});  // GCC (debug and release) and LLVM
    });

    return s.run(argc, argv);
}
