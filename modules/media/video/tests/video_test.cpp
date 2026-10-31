// openmui.video: frames and samples on exact counts (24, 30, 60 and
// 30000/1001 fps at 48 kHz, for ten minutes); the integer RGB → Y'CbCr
// conversion (black, white, greys, the primaries, a round trip); the sync
// card's flash and beep on the same frame, to the sample; the built-in
// encoders — Y4M read back as what went in, a PNG sequence, a WAV the frames'
// length — with their bytes pinned on every platform; a description read,
// its problems located, written back as itself; and a sound track whose cues
// land on their frames' first samples.
import std;
import hanim;
import openmui.image;
import openmui.audio;
import openmui.video;
import openmui.testing;

using namespace omui::testing;
namespace video = omui::video;
namespace imageio = omui::imageio;

namespace {

std::string hex(std::uint64_t h) { return std::format("{:016x}", h); }

// Every file an encode wrote, hashed together (names and bytes).
std::uint64_t all_files(const video::Encoded& e) {
    Hash h;
    for (const auto& f : e.files) {
        h.str(f.name);
        h.u64(f.hash);
    }
    return h.value();
}

imageio::Image solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    imageio::Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) img.rgba[i] = r, img.rgba[i + 1] = g, img.rgba[i + 2] = b, img.rgba[i + 3] = a;
    return img;
}

constexpr std::string_view sounds_file = R"(
[sound.click]
[[sound.click.layer]]
wave = "square"
length = 0.02
pitch = [2000, 2000]
envelope = { attack = 0, hold = 0.01, decay = 0.01, sustain = 0, release = 0 }
)";

}  // namespace

int main(int argc, char** argv) {
    Suite s("openmui.video");

    s.test("timing: 24, 30 and 60 fps at 48 kHz, every frame its exact samples for ten minutes", [] {
        for (const auto [fps, per] : {std::pair{24, 2000}, std::pair{30, 1600}, std::pair{60, 800}}) {
            const video::Timing t{{fps, 1}, 48000};
            const std::int64_t frames = 600LL * fps;
            bool exact = true;
            for (std::int64_t i = 0; i < frames; ++i) exact = exact && t.samples_in(i) == per && t.sample_of(i) == i * per && t.frame_of_sample(i * per + per - 1) == i;
            check(exact, std::format("{} fps", fps));
            check_eq(t.sample_of(frames), 600LL * 48000);
            check_eq(t.frames_in(3.0), 3LL * fps);
            check_eq(t.time_of(fps, 1'000'000'000), std::int64_t{1'000'000'000});
            check_eq(t.frame_at(1.0), static_cast<std::int64_t>(fps));
        }
    });

    s.test("timing: 30000/1001 — five frames in exactly 8008 samples, never drifting; a sample's frame is the one it plays under", [] {
        const video::Timing t{{30000, 1001}, 48000};
        check_eq(t.sample_of(5), std::int64_t{8008});
        std::int64_t sum = 0;
        bool sizes = true;
        for (std::int64_t i = 0; i < 30000; ++i) {
            const auto n = t.samples_in(i);
            sizes = sizes && (n == 1601 || n == 1602);
            sum += n;
        }
        check(sizes);
        check_eq(sum, std::int64_t{30000} * 8008 / 5);  // 1001 seconds of 30000 frames: 48048000 samples
        check_eq(t.sample_of(30000), std::int64_t{48'048'000});
        bool inverse = true;
        for (std::int64_t sample = 0; sample < 200000; sample += 7) {
            const auto f = t.frame_of_sample(sample);
            inverse = inverse && t.sample_of(f) <= sample && sample < t.sample_of(f + 1);
        }
        check(inverse);
        check_eq(t.frames_in(3.0), std::int64_t{90});  // the 90th starts at 2.97 s
        check_eq(t.time_of(1, 1'000'000'000), std::int64_t{33'366'666});
        check_eq(t.time_of(30000, 1000), std::int64_t{1'001'000});
        check(video::parse_rate("29.97") == hanim::FrameRate{30000, 1001});
        check(video::parse_rate("24000/1001") == hanim::FrameRate{24000, 1001});
        check(!video::parse_rate("0/1") && !video::parse_rate("x"));
        check_eq(video::rate_text({60000, 1001}), std::string("60000/1001"));
    });

    s.test("Y'CbCr: black 16, white 235, greys colourless, the primaries where BT.709 puts them; a round trip within 2", [] {
        auto px = [](std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            const auto img = solid(2, 2, r, g, b);
            const auto y = video::to_yuv420(imageio::view(img));
            return std::array<int, 3>{y.y[0], y.u[0], y.v[0]};
        };
        check(px(0, 0, 0) == std::array{16, 128, 128});
        check(px(255, 255, 255) == std::array{235, 128, 128});
        bool grey = true;
        for (int v = 0; v < 256; ++v) {
            const auto c = px(static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v));
            grey = grey && c[1] == 128 && c[2] == 128 && std::abs(c[0] - (16 + (219 * v + 127) / 255)) <= 1;
        }
        check(grey);
        // BT.709 limited range: red (63, 102, 240), green (173, 42, 26), blue (32, 240, 118).
        check(px(255, 0, 0) == std::array{63, 102, 240}, std::format("red {}", px(255, 0, 0)));
        check(px(0, 255, 0) == std::array{173, 42, 26}, std::format("green {}", px(0, 255, 0)));
        check(px(0, 0, 255) == std::array{32, 240, 118}, std::format("blue {}", px(0, 0, 255)));
        int worst = 0;
        for (int r = 0; r < 256; r += 15)
            for (int g = 0; g < 256; g += 15)
                for (int b = 0; b < 256; b += 15) {
                    const auto img = solid(4, 4, static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b));
                    const auto back = video::to_rgba(video::to_yuv420(imageio::view(img)));
                    for (int k = 0; k < 3; ++k) worst = std::max(worst, std::abs(back.rgba[static_cast<std::size_t>(k)] - img.rgba[static_cast<std::size_t>(k)]));
                }
        check(worst <= 2, std::format("worst {}", worst));
        // Not opaque counts as over black, straight or premultiplied alike.
        const auto half = solid(2, 2, 255, 255, 255, 128);
        auto pre = half;
        pre.alpha = imageio::Alpha::premultiplied;
        for (std::size_t i = 0; i < pre.rgba.size(); i += 4) pre.rgba[i] = pre.rgba[i + 1] = pre.rgba[i + 2] = 128;
        check(video::to_yuv420(imageio::view(half)) == video::to_yuv420(imageio::view(pre)));
        // Odd sizes: the edge blocks are their own pixels.
        const auto odd = solid(5, 3, 10, 200, 30);
        const auto y = video::to_yuv420(imageio::view(odd));
        check(y.chroma_width() == 3 && y.chroma_height() == 2 && y.u.size() == 6 && y.u[5] == y.u[0]);
    });

    s.test("the card: the flash and the beep on the frame each second begins in, to the sample", [] {
        for (const hanim::FrameRate fps : {hanim::FrameRate{24, 1}, hanim::FrameRate{30, 1}, hanim::FrameRate{60, 1}, hanim::FrameRate{30000, 1001}}) {
            const video::Format f{.width = 96, .height = 54, .timing = {fps, 48000}, .frames = video::Timing{fps, 48000}.frames_in(4.0)};
            const auto track = video::card_track(f);
            check_eq(track.size(), static_cast<std::size_t>(f.samples() * 2));
            std::vector<std::int64_t> flashes;
            for (std::int64_t i = 0; i < f.frames; ++i)
                if (video::card_flash(f.timing, i)) flashes.push_back(i);
            // One a second begun within the video (4 s at 30000/1001 is 120 frames: 4.004 s, five).
            check_eq(flashes.size(), static_cast<std::size_t>((f.samples() + 47999) / 48000));
            for (std::size_t k = 0; k < flashes.size(); ++k) {
                const auto i = flashes[k];
                check(f.timing.sample_of(i) <= static_cast<std::int64_t>(k) * 48000 && static_cast<std::int64_t>(k) * 48000 < f.timing.sample_of(i + 1));
                // The beep sounds from the flash frame's first sample to its last, and not a sample beyond.
                const auto first = f.timing.sample_of(i), last = f.timing.sample_of(i + 1) - 1;
                check(track[static_cast<std::size_t>(first * 2 + 2)] != 0.0f && track[static_cast<std::size_t>(last * 2)] != 0.0f);
                if (first > 0) check(track[static_cast<std::size_t>(first * 2 - 2)] == 0.0f);
                check(static_cast<std::size_t>(last * 2 + 2) >= track.size() || track[static_cast<std::size_t>(last * 2 + 2)] == 0.0f);
            }
            // Every frame of a second is a different picture (the sweep and the counter move).
            video::Card card(f);
            std::set<std::uint64_t> seen;
            const auto n = std::min<std::int64_t>(f.frames, (fps.num + fps.den - 1) / fps.den);
            for (std::int64_t i = 0; i < n; ++i) {
                const auto v = card.frame(i);
                seen.insert(video::hash(std::span(v->rgba, static_cast<std::size_t>(v->stride) * v->height)));
            }
            check_eq(seen.size(), static_cast<std::size_t>(n));
        }
    });

    s.test("Y4M: read back as the frames that went in; the bytes pinned", [] {
        const video::Format f{.width = 64, .height = 36, .timing = {{30, 1}, 48000}, .frames = 45};
        video::Card card(f);
        video::MemoryOutput out;
        auto y4m = video::y4m_encoder("card.y4m");
        std::array<video::Encoder*, 1> enc{y4m.get()};
        const auto done = video::encode(f, card, {}, enc, out);
        check(done.has_value());
        if (!done) return;
        const auto bytes = out.bytes("card.y4m");
        const auto header = video::y4m_header(64, 36, {30, 1});
        check_eq(bytes.size(), header.size() + 45 * (6 + 64 * 36 * 3 / 2));
        const auto back = video::read_y4m(bytes);
        check(back.has_value());
        if (!back) return;
        check(back->width == 64 && back->height == 36 && back->fps == hanim::FrameRate{30, 1} && back->frames.size() == 45);
        video::Card again(f);
        bool same = true;
        for (std::int64_t i = 0; i < 45; ++i) same = same && back->frames[static_cast<std::size_t>(i)] == video::to_yuv420(*again.frame(i));
        check(same);
        check(std::isinf(video::psnr(back->frames[3], back->frames[3]).y));
        std::println("    y4m {}", hex(done->files[0].hash));
        check_eq(hex(done->files[0].hash), std::string("9292219e7b9abdbf"));  // integers only: every compiler, every platform
    });

    s.test("a PNG sequence and a WAV the frames' length; the same bytes in memory and on disk, every time", [] {
        const video::Format f{.width = 64, .height = 36, .timing = {{24, 1}, 48000}, .frames = 30};
        const auto track = video::card_track(f);
        auto run = [&](video::Output& out) {
            video::Card card(f);
            auto png = video::png_encoder("png/{:04}.png");
            auto wav = video::wav_encoder("card.wav");
            std::array<video::Encoder*, 2> enc{png.get(), wav.get()};
            return video::encode(f, card, track, enc, out);
        };
        video::MemoryOutput mem;
        const auto a = run(mem);
        check(a.has_value());
        if (!a) return;
        check_eq(a->files.size(), std::size_t{31});
        check_eq(a->files[0].name, std::string("png/0000.png"));
        check_eq(a->samples, std::int64_t{60000});  // 30 frames at 24 fps: 1.25 s
        const auto wav = omui::audio::read_wav(mem.bytes("card.wav"));
        check(wav.has_value() && wav->samples.size() == 60000 && wav->rate == 48000);
        check_eq(mem.bytes("card.wav").size(), std::size_t{44 + 60000 * 2 * 2});  // 16-bit stereo
        video::DirectoryOutput disk(scratch_dir("video-png"));
        const auto b = run(disk);
        check(b.has_value() && all_files(*a) == all_files(*b));
        video::MemoryOutput again;
        check(all_files(*run(again)) == all_files(*a));
        std::println("    png+wav {}", hex(all_files(*a)));
        check_eq(hex(all_files(*a)), std::string("fad8b6d45c90a492"));  // libpng and zlib as mcpp.lock pins them
    });

    s.test("a track shorter than the frames is filled with silence; a longer one is cut", [] {
        const video::Format f{.width = 16, .height = 16, .timing = {{60, 1}, 48000}, .frames = 6};
        std::vector<float> shorter(2 * 1000, 0.25f), longer(2 * 9000, 0.25f);
        for (const auto* track : {&shorter, &longer}) {
            video::Card card(f);
            video::MemoryOutput out;
            auto wav = video::wav_encoder("a.wav", true);
            std::array<video::Encoder*, 1> enc{wav.get()};
            check(video::encode(f, card, *track, enc, out).has_value());
            const auto r = omui::audio::read_wav(out.bytes("a.wav"));
            check(r.has_value() && r->samples.size() == 4800);
            if (r) check(r->samples[999] == 0.25f && (track == &longer ? r->samples[4799] == 0.25f : r->samples[1000] == 0.0f));
        }
    });

    s.test("a description: read, its problems at their line and column, written back as itself", [] {
        constexpr std::string_view text = R"(doc = "A test."

[video]
width = 320
height = 180
fps = "30000/1001"
seconds = 3

[frames]
png = "shots/{:05}.png"

[audio]
music = "theme.music.omui.toml"
intensity = [{ at = 0, value = 0.3 }, { at = 2, value = 1 }]
sounds = "ui.sound.omui.toml"
recordings = { voice = "voice.wav" }
cues = [
    { sound = "click", frame = 30 },
    { sound = "whoosh", at = 1.25, gain = 0.8, pan = -0.5 },
]
beeps = true
)";
        const auto d = video::read_description(text, "t.video.omui.toml");
        check(d.has_value());
        if (!d) {
            for (const auto& p : d.error()) std::println("    {}", p.text());
            return;
        }
        check(d->fps == hanim::FrameRate{30000, 1001} && d->frames == 90 && d->from == video::FramesFrom::png && d->cues.size() == 2 && d->intensity.size() == 2);
        check(d->format().samples() == video::Timing{{30000, 1001}, 48000}.sample_of(90) && d->format().channels == 2);
        const std::string written = video::write_description(*d);
        check_eq(written, std::string(text));
        const auto back = video::read_description(written, "t");
        check(back.has_value() && *back == *d);

        const auto bad = video::read_description("[video]\nwidht = 3\nfps = 0\nseconds = 1\n[frames]\ncard = \"synk\"\n[audio]\ncues = [{ sound = \"x\", at = 1, frame = 2 }]\n", "bad.video.omui.toml");
        check(!bad.has_value());
        if (bad) return;
        std::vector<std::string> texts;
        for (const auto& p : bad.error()) texts.push_back(p.text());
        auto has = [&](std::string_view s) { return std::ranges::any_of(texts, [&](const std::string& t) { return t.find(s) != std::string::npos; }); };
        check(has("bad.video.omui.toml:2:1: [video] has no key `widht` (did you mean `width`?)"), std::format("{}", texts));
        check(has("bad.video.omui.toml:3:7: `fps`"), std::format("{}", texts));
        check(has("no card `synk` (did you mean `sync`?)"), std::format("{}", texts));
        check(has("a cue has `at` or `frame`, not both"), std::format("{}", texts));
        check(!video::read_description("[video\n", "x").has_value());
    });

    s.test("a sound track: each cue on its frame's first sample, through the mixer; the bits pinned", [] {
        const auto dir = scratch_dir("video-soundtrack");
        std::ofstream(dir / "ui.sound.omui.toml") << sounds_file;
        const auto d = video::read_description(R"([video]
width = 64
height = 36
fps = 30
seconds = 2

[audio]
sounds = "ui.sound.omui.toml"
cues = [{ sound = "click", frame = 30 }, { sound = "click", at = 0.5, pan = -1 }]
)",
                                               "t");
        check(d.has_value());
        if (!d) return;
        const auto track = video::soundtrack(*d, dir);
        check(track.has_value());
        if (!track) {
            for (const auto& p : track.error()) std::println("    {}", p.text());
            return;
        }
        check_eq(track->size(), std::size_t{2 * 96000});
        auto first_sound = [&](std::size_t from) {
            for (std::size_t i = from; i < track->size() / 2; ++i)
                if ((*track)[2 * i] != 0.0f || (*track)[2 * i + 1] != 0.0f) return i;
            return track->size();
        };
        // The panned cue at 0.5 s: from sample 24000 (its first sample may be the envelope's zero).
        const auto a = first_sound(0);
        check(a >= 24000 && a <= 24001, std::format("first sound at {}", a));
        const auto b = first_sound(24000 + 2400);
        check(b >= 48000 && b <= 48001, std::format("frame 30's cue at {}", b));
        // Unknown sounds and missing files are problems.
        auto no = *d;
        no.cues.push_back({.sound = "boom"});
        no.music = "missing.music.omui.toml";
        const auto bad = video::soundtrack(no, dir);
        check(!bad.has_value() && bad.error().size() == 2);
        std::println("    soundtrack {}", hex(omui::audio::hash(*track)));
        check_eq(hex(omui::audio::hash(*track)), std::string("0a7543c6a083a995"));
    });

    return s.run(argc, argv);
}
