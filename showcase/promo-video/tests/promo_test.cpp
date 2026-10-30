// S5, openmui's film, as its acceptance (promo-video design §5.1) holds it:
// both cuts read and hold together; their duration, spec, lines, beats and
// the onsets heard in the sound; frames of each chapter pinned on every
// platform (and one at 4K); frames drawn directly the frames played to; the
// typography of sampled frames inside the title-safe area and apart, none
// blank, at 16:9 and 9:16; the rewind the film's own frames, backwards, and
// the replay the first play's very frames. The full render is a command
// (`omui-promo render promo --out promo.webm`); the tests draw what CI can
// afford: pinned frames, short stretches, sampled frames at a quarter scale.
import std;
import openmui;
import openmui.timeline;
import openmui.audio;
import openmui.testing;
import openmui.showcase.promo_video;

using namespace omui::testing;
using namespace omui;
namespace tl = omui::timeline;

namespace {

const promo::Film& film() {
    static const promo::Film f = [] {
        auto r = promo::load_film();
        if (!r) fatal(std::format("the film does not read: {}", r.error().front().text()));
        return std::move(*r);
    }();
    return f;
}

promo::Cut cut(std::string_view name) {
    auto c = promo::load_cut(film().files, name, film().subtitles);
    if (!c) {
        for (const auto& p : c.error()) std::println("    {}", p.text());
        fatal(std::format("the cut `{}` does not read", name));
    }
    return std::move(*c);
}

std::string hex(std::uint64_t h) { return std::format("{:016x}", h); }

typeset::FontCollection& fonts() {
    static typeset::FontCollection f = [] {
        typeset::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"showcase.promo_video"};

    s.test("the film reads: two cuts, their lines, the score and the sounds; a mistake is found where it is", [] {
        const promo::Cut main = cut("promo"), short_cut = cut("short");
        check_eq(main.frames(), std::int64_t{3600});
        check_eq(short_cut.frames(), std::int64_t{900});
        check(main.picture >= 0 && main.subtitles >= 0 && main.sfx >= 0, "picture, subtitles and sfx tracks");
        check(film().subtitles.lines.size() >= 11, std::format("{} lines", film().subtitles.lines.size()));
        auto music = audio::read_music(*film().files.read("music/promo.music.omui.toml"), "promo.music.omui.toml");
        check(music.has_value() && music->tempo == 120, "the score reads, at 120 a minute");
        auto sounds = audio::read_sounds(*film().files.read("sounds/promo.sound.omui.toml"), "promo.sound.omui.toml");
        check(sounds.has_value() && sounds->size() >= 12, "the sounds read");
        // What every shot name and clip is: each shot made for its clip.
        promo::Player p(film(), main, {.scale = 0.25f, .fonts = &fonts(), .parallel_raster = false});
        for (std::size_t i = 0; i < main.timeline.tracks[static_cast<std::size_t>(main.picture)].clips.size(); ++i) {
            const auto& c = main.timeline.tracks[static_cast<std::size_t>(main.picture)].clips[i];
            if (c.source == "film") continue;
            check(p.make(i).has_value(), c.name);
        }
        // Mistakes, each found: a shot that is no shot, a line that is no line, a rewind into the future.
        std::string text = main.text;
        auto broken = [&](std::string_view from, std::string_view to) {
            std::string t = text;
            t.replace(t.find(from), from.size(), to);
            const auto dir = scratch_dir("promo-broken");
            std::ofstream(dir / "promo.timeline.omui.toml", std::ios::binary) << t;
            return promo::load_cut(promo::Files(dir), "promo", film().subtitles);
        };
        auto a = broken("shot:styles", "shot:stiles");
        check(!a && a.error().front().message.find("no shot is named `stiles`") != std::string::npos, "an unknown shot");
        auto b = broken("line:effects", "line:affects");
        check(!b && b.error().front().message.find("no line") != std::string::npos, "an unknown line");
        auto c = broken("speed = -5", "speed = 5");
        check(!c && c.error().front().message.find("before itself") != std::string::npos, "a rewind must play frames before it");
    });

    s.test("the acceptance's data: duration, spec, both languages up long enough to read, every cut and hit on the beat, its onset heard", [] {
        for (std::string_view name : promo::cut_names) {
            const promo::Cut c = cut(name);
            promo::Player p(film(), c, {.scale = 0.25f, .fonts = &fonts(), .parallel_raster = false});
            auto track = promo::soundtrack(film(), c, p);
            check(track.has_value(), track ? "" : track.error());
            if (!track) continue;
            const promo::Acceptance a = promo::check_data(film(), c, *track, {});
            for (const auto& v : a.verdicts) {
                std::println("    {:<6} {:<9} {} {}", name, v.name, v.ok ? "ok" : "FAILS", v.detail);
                check(v.ok, std::format("{}: {}: {}", name, v.name, v.detail));
            }
            for (const auto& f : a.findings) fail(std::format("{}: {} (frame {})", f.check, f.what, f.frame));
            // The sound pinned: the same samples everywhere.
            std::println("    {:<6} sound {} ({} cues)", name, hex(audio::hash(track->samples)), track->cues.size());
            check_eq(hex(audio::hash(track->samples)), std::string(name == "promo" ? "441f28809077b0f8" : "4cd8e041d7edf38a"));
        }
    });

    s.test("frames pinned on every platform: a frame of each chapter at half size, one at 1080p, one at 4K", [] {
        const promo::Cut c = cut("promo");
        // Each chapter's signature moment: the pixel, the wordmark, the counter four times, the wall, neon, the needs-answer, the rally, the rewind, the command.
        constexpr std::array<std::int64_t, 10> frames{60, 400, 900, 1500, 1820, 1830 + 30, 2470, 2900, 3030, 3420};
        promo::Player p(film(), c, {.scale = 0.5f, .fonts = &fonts(), .parallel_raster = false});
        Hash all;
        const auto dir = scratch_dir("promo-frames");  // once: it starts empty
        for (const std::int64_t f : frames) {
            auto px = p.frame(f);
            check(px.has_value(), px ? "" : px.error());
            if (!px) return;
            const auto h = promo::pixel_hash(**px);
            all.u64(h);
            std::println("    frame {:>4}: {}", f, hex(h));
            check(imageio::save_png(dir / std::format("{:04}.png", f), {reinterpret_cast<const std::uint8_t*>((*px)->row(0)), (*px)->width(), (*px)->height(),
                                                                          (*px)->width() * 4, imageio::Alpha::premultiplied})
                      .has_value());
        }
        check_eq(all.hex(), std::string("5447f140b0b6d34d"));
        promo::Player full(film(), c, {.scale = 1, .fonts = &fonts(), .parallel_raster = false});
        auto hd = full.frame(1860);
        check(hd && (*hd)->width() == 1920 && (*hd)->height() == 1080, "1080p");
        if (hd) check_eq(hex(promo::pixel_hash(**hd)), std::string("24808790abd37b91"));
        promo::Player uhd(film(), c, {.scale = 2, .fonts = &fonts(), .parallel_raster = false});
        auto k4 = uhd.frame(400);
        check(k4 && (*k4)->width() == 3840 && (*k4)->height() == 2160, "4K");
        if (k4) check_eq(hex(promo::pixel_hash(**k4)), std::string("4e3f3873159366fe"));
    });

    s.test("seek: a frame drawn directly is the frame played to — in every chapter, after its moments, and through the rewind", [] {
        const promo::Cut c = cut("promo");
        promo::Player p(film(), c, {.scale = 0.25f, .fonts = &fonts(), .parallel_raster = false});
        // Stretches played in order, then frames of them drawn directly.
        const std::array<std::pair<std::int64_t, std::int64_t>, 8> stretches{{{90, 130}, {700, 730}, {1270, 1300}, {1430, 1450}, {1790, 1830}, {2335, 2375}, {2785, 2800},
                                                                              {3000, 3012}}};
        std::size_t compared = 0;
        for (const auto& [a, b] : stretches) {
            std::map<std::int64_t, std::uint64_t> played;
            for (std::int64_t f = a; f < b; ++f) {
                auto px = p.frame(f);
                check(px.has_value(), px ? "" : px.error());
                if (!px) return;
                played[f] = promo::pixel_hash(**px);
            }
            for (const std::int64_t f : {a, (a + b) / 2, b - 1}) {
                auto d = p.direct(f);
                check(d.has_value() && promo::pixel_hash(*d) == played[f], std::format("frame {} drawn directly", f));
                ++compared;
            }
        }
        std::println("    {} frames drawn directly, each the frame played to", compared);
    });

    s.test("the film rewinds itself, and plays again: the rewind is its own frames backwards; the replay, the first play's very frames", [] {
        const promo::Cut c = cut("promo");
        const auto r = promo::shown(c, 3020);
        check(r && r->film && tl::frame_at(c.timeline.rate, r->source) == 2999 - 5 * 20, "frame 3020 plays back frame 2899");
        // Without the lines and the timeline (they say where in the film it is), the replay is the first play, byte for byte.
        promo::Player p(film(), c, {.scale = 0.25f, .subtitles = false, .timebar = false, .chapters = false, .fonts = &fonts(), .parallel_raster = false});
        std::vector<std::uint64_t> first, again;
        for (std::int64_t k = 0; k < 40; ++k) {
            auto a = p.frame(2760 + k);
            check(a.has_value());
            if (!a) return;
            first.push_back(promo::pixel_hash(**a));
        }
        for (std::int64_t k = 0; k < 40; ++k) {
            auto b = p.frame(3060 + k);
            check(b.has_value());
            if (!b) return;
            again.push_back(promo::pixel_hash(**b));
        }
        check(first == again, "the replay's frames are the first play's (before its stamp)");
        check(std::set<std::uint64_t>(first.begin(), first.end()).size() > 30, "and the game moves");
    });

    s.test("every sampled frame's typography at rest is inside the title-safe area and apart, and none is blank — 16:9, and the short at 9:16", [] {
        struct Run {
            std::string_view name;
            Size             size;
            std::int64_t     every;
        };
        for (const Run& run : {Run{"promo", {}, 30}, Run{"short", {}, 15}, Run{"short", {1080, 1920}, 15}}) {
            const promo::Cut c = cut(run.name);
            promo::Player p(film(), c, {.size = run.size, .scale = 0.25f, .fonts = &fonts(), .parallel_raster = false});
            std::size_t n = 0, problems = 0;
            for (const std::int64_t f : promo::sampled_frames(c, run.every)) {
                auto px = p.frame(f);
                check(px.has_value(), px ? "" : px.error());
                if (!px) return;
                for (const auto& finding : promo::check_frame(p, f, **px)) {
                    fail(std::format("{} {}×{} frame {}: {}: {}", run.name, run.size.width, run.size.height, f, finding.check, finding.what));
                    ++problems;
                }
                ++n;
            }
            std::println("    {} at {}: {} frames, {} problems", run.name, run.size.width > 0 ? "9:16" : "16:9", n, problems);
        }
    });

    s.test("the README's loop: a few seconds of every chapter — an animated WebP under 1 MB", [] {
        const promo::Cut c = cut("promo");
        promo::Player p(film(), c, {.scale = 0.25f, .fonts = &fonts(), .parallel_raster = false});
        // A beat of each chapter at 7.5 frames a second.
        constexpr std::array<std::pair<std::int64_t, std::int64_t>, 8> beats{{{40, 160}, {330, 450}, {900, 960}, {1260, 1500}, {1740, 2040}, {2400, 2580}, {2880, 3060}, {3360, 3600}}};
        std::vector<imageio::Image> kept;
        for (const auto& [a, b] : beats)
            for (std::int64_t f = a; f < b; f += 8) {
                auto px = p.frame(f);
                check(px.has_value());
                if (!px) return;
                imageio::Image img;
                img.width = (*px)->width();
                img.height = (*px)->height();
                img.alpha = imageio::Alpha::premultiplied;
                img.rgba.assign(reinterpret_cast<const std::uint8_t*>((*px)->row(0)),
                                reinterpret_cast<const std::uint8_t*>((*px)->row(0)) + static_cast<std::size_t>(img.width) * img.height * 4);
                kept.push_back(std::move(img));
            }
        std::vector<imageio::View> views;
        for (const auto& k : kept) views.push_back(imageio::view(k));
        const auto file = scratch_dir("readme-promo-video") / "film.webp";
        check(imageio::save_animated_webp(file, views, {.delay_num = 2, .delay_den = 15, .quality = 72}).has_value());
        const auto size = std::filesystem::file_size(file);
        std::println("    the loop: {} frames, {} KB", views.size(), size / 1024);
        check(size < 1024 * 1024, std::format("{} bytes", size));
    });

    return s.run(argc, argv);
}
