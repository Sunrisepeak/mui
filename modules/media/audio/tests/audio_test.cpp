// Sounds as data and the mixer: a sound file read and written back, its
// problems at their line and column; a sine at its pitch; a sound starting on
// its sample to the sample; an envelope's shape; a glide reaching its pitch;
// a lowpass taking off the highs; voices taken oldest first; the limiter;
// nothing allocated while rendering; one thread playing while another
// renders; and a scene rendered offline, its bits pinned on every platform.
import std;
import openmui.audio;
import openmui.testing;

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
// The nothrow forms too (a library's temporary buffer asks for them): every
// form this program's delete frees comes from this program's malloc.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    allocations.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(size ? size : 1);
}
void* operator new[](std::size_t size, const std::nothrow_t& t) noexcept { return ::operator new(size, t); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace {

constexpr std::string_view file = R"(doc = "Two sounds: a punch and a bell."

[sound.punch]
gain = 0.9

[[sound.punch.layer]]
wave = "noise"
length = 0.09
envelope = { attack = 0.001, decay = 0.08, sustain = 0, release = 0.01 }
filter = { kind = "lowpass", cutoff = [4000, 500], q = 0.8 }
gain = 0.7
seed = 7

[[sound.punch.layer]]
wave = "sine"
length = 0.16
pitch = [170, 55]
envelope = { attack = 0.002, decay = 0.14, sustain = 0, release = 0.02 }
drive = 0.3

[sound.bell]
bus = "music"

[[sound.bell.layer]]
wave = "sine"
length = 1.2
pitch = 660
envelope = { attack = 0.003, decay = 1.1, sustain = 0, release = 0.05 }

[[sound.bell.layer]]
wave = "triangle"
at = 0.01
length = 0.8
pitch = 1650
envelope = { attack = 0.003, decay = 0.7, sustain = 0, release = 0.05 }
gain = 0.3
pan = 0.4
)";

audio::Sound tone(float hz, float seconds, audio::Wave wave = audio::Wave::sine) {
    audio::Sound s{.name = std::format("tone{}", hz)};
    s.layers.push_back({.wave = wave, .length = seconds, .pitch = {hz, hz}, .envelope = {.attack = 0.0001f, .hold = 10, .decay = 0.001f, .sustain = 1, .release = 0.0001f}});
    return s;
}

// Zero crossings upward in the left channel of [from, to) frames.
int crossings(std::span<const float> lr, std::size_t from, std::size_t to) {
    int n = 0;
    for (std::size_t i = from + 1; i < to; ++i) n += lr[2 * (i - 1)] < 0 && lr[2 * i] >= 0;
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"audio"};

    s.test("a sound file reads, and what is written reads back as itself; its problems at their line and column", [] {
        auto sounds = audio::read_sounds(file, "test.sound.omui.toml");
        check(sounds.has_value(), sounds ? "" : sounds.error().front().text());
        if (!sounds) return;
        auto named = [&](std::string_view n) { return *std::ranges::find(*sounds, n, &audio::Sound::name); };  // by name: a file's sounds come in name order
        check(sounds->size() == 2 && named("punch").layers.size() == 2 && named("bell").bus == "music");
        const audio::Sound punch = named("punch");  // a copy: kept while its layer is read
        const audio::Layer& thump = punch.layers[1];
        check(thump.pitch[0] == 170 && thump.pitch[1] == 55 && thump.drive == 0.3f, "the thump falls from 170 to 55 Hz");
        check_near(named("punch").length(), 0.16, 1e-6);
        const std::string written = audio::write_sounds(*sounds);
        auto again = audio::read_sounds(written, "written.sound.omui.toml");
        check(again && audio::write_sounds(*again) == written, "written back as itself");
        auto bad = audio::read_sounds("[sound.x]\n[[sound.x.layer]]\nwave = \"sqaure\"\nptich = 3\nlength = -1\n", "bad.sound.omui.toml");
        check(!bad.has_value());
        if (bad) return;
        std::vector<std::string> texts;
        for (const auto& p : bad.error()) texts.push_back(p.text());
        auto has = [&](std::string_view t) { return std::ranges::any_of(texts, [&](const std::string& x) { return x == t; }); };
        check(has("bad.sound.omui.toml:3:8: `sqaure` is not a wave (sine, triangle, square, saw, noise, recording) (did you mean `square`?)"), std::format("{}", texts));
        check(has("bad.sound.omui.toml:4:1: sound `x`'s layer has no key `ptich` (did you mean `pitch`?)"), std::format("{}", texts));
        check(has("bad.sound.omui.toml:5:10: a layer's `length` (seconds) is 0.001–60, not -1"), std::format("{}", texts));
    });

    s.test("a recording plays as recorded, faster and slower, at another rate; a WAV read back; a recording's layer read and written back", [] {
        std::vector<float> r(1000);
        for (std::size_t i = 0; i < r.size(); ++i) r[i] = 0.5f * static_cast<float>(std::sin(static_cast<double>(i) * 0.05));
        // Held for its whole length: what is heard is the recording, sample for sample, until its release.
        auto heard = [&](std::int32_t rate, std::array<float, 2> speed) {
            audio::Mixer m;
            m.add_recording("take", r, rate);
            audio::Sound s{.name = "voice"};
            s.layers.push_back({.wave = audio::Wave::recording, .length = 0, .envelope = {.attack = 0, .hold = 10, .decay = 0.001f, .sustain = 1, .release = 0.0001f},
                                .recording = "take", .speed = speed});
            check(m.play(m.add(s), 0));
            return audio::render(m, 4000);
        };
        auto at = [&](double place) {  // the recording between two samples
            const auto n = static_cast<std::size_t>(place);
            return n + 1 < r.size() ? r[n] + (r[n + 1] - r[n]) * (place - static_cast<double>(n)) : 0.0;
        };
        auto follows = [&](const std::vector<float>& out, double step, std::size_t frames) {
            double worst = 0;
            for (std::size_t i = 1; i + 8 < frames; ++i) worst = std::max(worst, std::abs(out[2 * i] - at(static_cast<double>(i) * step)));
            const bool ends = out[2 * (frames + 2)] == 0 && out[2 * (frames + 2) + 1] == 0;
            return worst < 1e-6 && ends;
        };
        check(follows(heard(48000, {1, 1}), 1, 1000), "as recorded: the same samples, then silence");
        check(follows(heard(48000, {2, 2}), 2, 500), "twice as fast: every other sample, half as long");
        check(follows(heard(24000, {1, 1}), 0.5, 2000), "recorded at 24 kHz: read between its samples, twice as long");
        // A WAV of it, 16-bit and float, read back: its two channels mixed to one.
        std::vector<float> stereo;
        for (float v : r) stereo.push_back(v), stereo.push_back(v * 0.5f);
        const auto f32 = audio::read_wav(audio::wav(stereo, 22050, true));
        const auto i16 = audio::read_wav(audio::wav(stereo, 22050));
        check(f32 && i16 && f32->rate == 22050 && f32->samples.size() == r.size() && i16->samples.size() == r.size(), "both read");
        if (!f32 || !i16) return;
        double f_err = 0, i_err = 0;
        for (std::size_t i = 0; i < r.size(); ++i) {
            f_err = std::max(f_err, std::abs(f32->samples[i] - r[i] * 0.75));
            i_err = std::max(i_err, std::abs(i16->samples[i] - r[i] * 0.75));
        }
        check(f_err < 1e-7 && i_err < 1.0 / 32768, std::format("float off by {}, 16-bit by {}", f_err, i_err));
        check(!audio::read_wav(std::as_bytes(std::span(std::string_view("not a wave file at all")))).has_value(), "not a WAV");
        // The file's words for it.
        const std::string_view text = "[sound.voice]\n[[sound.voice.layer]]\nwave = \"recording\"\nrecording = \"fight\"\nspeed = [1, 0.9]\nfilter = { kind = \"highpass\", cutoff = 120 }\n";
        auto voice = audio::read_sounds(text, "voice.sound.omui.toml");
        check(voice && voice->front().layers[0].recording == "fight" && voice->front().layers[0].speed[1] == 0.9f && voice->front().layers[0].length == 0, "read");
        if (!voice) return;
        const std::string written = audio::write_sounds(*voice);
        auto again = audio::read_sounds(written, "written.sound.omui.toml");
        check(again && audio::write_sounds(*again) == written, written);
        auto bad = audio::read_sounds("[sound.x]\n[[sound.x.layer]]\nwave = \"recording\"\n[[sound.x.layer]]\nwave = \"sine\"\nspeed = 2\n", "bad.sound.omui.toml");
        check(!bad.has_value() && bad.error().size() == 2, "a recording's layer without its recording; speed on a sine");
    });

    s.test("a recording's takes: a sung note and a hiss apart by silence, found where they are, the note's pitch heard and the hiss bright and unvoiced", [] {
        audio::Recording r{.samples = std::vector<float>(48000), .rate = 48000};
        for (std::size_t i = 4800; i < 19200; ++i) r.samples[i] = 0.4f * static_cast<float>(std::sin(2 * 3.141592653589793 * 220 * static_cast<double>(i) / 48000));
        std::uint32_t noise = 7;
        for (std::size_t i = 33600; i < 43200; ++i) {
            noise ^= noise << 13, noise ^= noise >> 17, noise ^= noise << 5;
            r.samples[i] = 0.3f * static_cast<float>(static_cast<std::int32_t>(noise)) / 2147483648.0f;
        }
        const auto t = audio::takes(r);
        check_eq(t.size(), std::size_t{2});
        if (t.size() != 2) return;
        check(std::abs(t[0].begin - 4320) <= 480 && std::abs(t[0].end - 19680) <= 480, std::format("the note at {}–{}", t[0].begin, t[0].end));
        check(std::abs(t[1].begin - 33120) <= 480 && std::abs(t[1].end - 43680) <= 480, std::format("the hiss at {}–{}", t[1].begin, t[1].end));
        check(std::abs(t[0].pitch - 220) < 3 && t[0].voiced > 0.9, std::format("the note at {} Hz, {} voiced", t[0].pitch, t[0].voiced));
        check(t[1].voiced < 0.2 && t[1].brightness > 10 * t[0].brightness, std::format("the hiss {} voiced, {} against {} crossings a second", t[1].voiced, t[1].brightness, t[0].brightness));
    });

    s.test("a sine plays at its pitch; a sound starts on its sample, to the sample", [] {
        audio::Mixer m;
        const auto a = m.add(tone(440, 1.0f));
        check(m.play(a, 0));
        const auto out = audio::render(m, 48000);
        const int n = crossings(out, 0, 48000);
        check(n >= 439 && n <= 441, std::format("{} cycles in a second", n));
        // From sample 1000 of the clock: silence before it, then the very samples it makes from 0, 1000 later.
        auto from = [](std::int64_t at) {
            audio::Mixer m2;
            m2.play(m2.add(tone(440, 0.05f, audio::Wave::square)), at);
            return audio::render(m2, 4000);
        };
        const auto early = from(0), late = from(1000);
        bool silent = true, same = true;
        for (std::size_t i = 0; i < 2000; ++i) silent = silent && late[i] == 0;
        for (std::size_t i = 0; i < 2 * 3000; ++i) same = same && late[2000 + i] == early[i];
        check(silent && same, "the same samples, 1000 later");
    });

    s.test("an envelope's shape: up over its attack, down to its sustain, silent at its end; a glide reaches its pitch", [] {
        audio::Sound s{.name = "shape"};
        s.layers.push_back({.wave = audio::Wave::square, .length = 0.5f, .pitch = {100, 100},
                            .envelope = {.attack = 0.1f, .hold = 0, .decay = 0.1f, .sustain = 0.5f, .release = 0.1f}});
        audio::Mixer m;
        m.play(m.add(s), 0);
        const auto out = audio::render(m, 24000);
        auto level = [&](double at) {  // the loudest over a 10 ms window there
            const auto from = static_cast<std::size_t>(at * 48000);
            return audio::peak(std::span(out).subspan(2 * from, 2 * 480));
        };
        check(level(0.02) < level(0.09) && level(0.09) > 0.8, std::format("rising: {} then {}", level(0.02), level(0.09)));
        check(level(0.3) > 0.4 && level(0.3) < 0.6, std::format("sustained at half: {}", level(0.3)));
        check(level(0.49) < 0.1, std::format("released: {}", level(0.49)));
        // 200 → 800 Hz over a second: near 800 at its end.
        audio::Sound g{.name = "glide"};
        g.layers.push_back({.length = 1.0f, .pitch = {200, 800}, .envelope = {.attack = 0.0001f, .hold = 10, .decay = 0.001f, .sustain = 1, .release = 0.0001f}});
        audio::Mixer gm;
        gm.play(gm.add(g), 0);
        const auto glide = audio::render(gm, 48000);
        const int start = crossings(glide, 0, 4800), end = crossings(glide, 43200, 48000);
        // The last 100 ms glide from 4^0.9 × 200 ≈ 696 Hz to 800: about 74 cycles.
        check(start >= 19 && start <= 23 && end >= 72 && end <= 76, std::format("{} then {} cycles in 100 ms", start, end));
    });

    s.test("a lowpass takes off the highs: filtered noise moves less from sample to sample", [] {
        auto roughness = [](audio::FilterKind f) {
            audio::Sound s{.name = "noise"};
            s.layers.push_back({.wave = audio::Wave::noise, .length = 0.2f, .envelope = {.attack = 0.0001f, .hold = 10, .decay = 0.001f, .sustain = 1, .release = 0.0001f},
                                .filter = f, .cutoff = {800, 800}, .gain = 0.5f});
            audio::Mixer m;
            m.play(m.add(s), 0);
            const auto out = audio::render(m, 9600);
            double r = 0;
            for (std::size_t i = 1; i < 9600; ++i) r += std::abs(out[2 * i] - out[2 * (i - 1)]);
            return r;
        };
        const double raw = roughness(audio::FilterKind::none), low = roughness(audio::FilterKind::lowpass);
        check(low < raw / 5, std::format("{} filtered, {} raw", low, raw));
    });

    s.test("voices: past the most at once, the oldest gives way; the limiter holds loud ones under its ceiling", [] {
        audio::Mixer m({.voices = 4});
        const auto t = m.add(tone(220, 1.0f));
        for (int i = 0; i < 10; ++i) m.play(t, i * 10);
        const auto out = audio::render(m, 4800);
        check_eq(m.sounding(), std::int32_t{4});
        check(audio::peak(out) <= 0.89f + 1e-6f, std::format("peak {}", audio::peak(out)));
    });

    s.test("nothing is allocated while playing and rendering", [] {
        auto sounds = *audio::read_sounds(file, "test.sound.omui.toml");
        audio::Mixer m;
        std::vector<std::int32_t> ids;
        for (const auto& snd : sounds) ids.push_back(m.add(snd));
        m.add_effect("sfx", audio::echo(0.12f, 0.35f, 0.3f, 48000));
        std::vector<float> block(2 * 256);
        m.render(block);  // a first block (a trace buffer, if any, made once)
        const std::int64_t before = allocations.load();
        for (int b = 0; b < 400; ++b) {
            if (b % 7 == 0) m.play(ids[static_cast<std::size_t>(b % 2)], m.clock() + 37, 0.8f, (b % 3 - 1) * 0.5f);
            m.render(block);
        }
        check_eq(allocations.load() - before, std::int64_t{0});
    });

    s.test("one thread plays while another renders: every play begins, none lost", [] {
        if constexpr (!threads) return;  // the Web row runs no threads
        audio::Mixer m({.voices = 64, .queue = 64});
        const auto t = m.add(tone(330, 0.01f));
        std::atomic<bool> done{false};
        std::jthread renderer([&] {
            std::vector<float> block(2 * 128);
            while (!done.load() || m.begun() < 200) m.render(block);
        });
        for (int i = 0; i < 200; ++i) {
            while (!m.play(t, 0)) std::this_thread::yield();
            if (i % 10 == 0) std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
        done = true;
        renderer.join();
        check_eq(m.begun(), std::int64_t{200});
    });

    s.test("a scene rendered offline — punches, a bell, an echo on the effects — is the same bits everywhere; a WAV of it", [] {
        auto sounds = *audio::read_sounds(file, "test.sound.omui.toml");  // the bell, then the punch (name order)
        audio::Mixer m;
        const auto bell = m.add(sounds[0]), punch = m.add(sounds[1]);
        m.add_effect("sfx", audio::echo(0.09f, 0.3f, 0.25f, 48000));
        m.add_effect("music", audio::filter(audio::FilterKind::highpass, 120));
        for (int k = 0; k < 6; ++k) m.play(punch, 4800 * k + 77 * k, 1.0f - 0.1f * k, k % 2 ? -0.6f : 0.6f, 1.0f + 0.05f * k);
        m.play(bell, 30000, 0.7f);
        const auto out = audio::render(m, 96000);
        check(audio::peak(out) > 0.2f && audio::peak(out) <= 0.89f + 1e-6f, std::format("peak {}", audio::peak(out)));
        const auto w = audio::wav(out, 48000);
        check(w.size() == 44 + out.size() * 2 && static_cast<char>(w[0]) == 'R' && static_cast<char>(w[8]) == 'W', "a 16-bit WAV");
        const auto dir = scratch_dir("audio");
        std::ofstream(dir / "scene.wav", std::ios::binary).write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
        std::println("    scene {:016x}", audio::hash(out));
        check_eq(audio::hash(out), std::uint64_t{0xe332a3d4a569c772});  // GCC, LLVM and wasm
    });

    return s.run(argc, argv);
}
