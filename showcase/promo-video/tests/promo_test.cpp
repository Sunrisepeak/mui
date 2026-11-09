// S5, openmui's film, as its acceptance (promo-video design §5.1) holds it:
// both cuts read and hold together; their duration, spec, lines, beats and
// the onsets heard in the sound; frames of each chapter pinned on every
// platform (and one at 4K); frames drawn directly the frames played to; the
// typography of sampled frames inside the title-safe area, apart and no
// smaller than the film's least (painters' labels too), none
// blank, at 16:9 and 9:16; the rewind the film's own frames, backwards, and
// the replay the first play's very frames. The full render is a command
// (`omui-promo render promo --out promo.webm`); the tests draw what CI can
// afford: pinned frames, short stretches, sampled frames at a quarter scale.
import std;
import omui;
import xengine.timeline;
import xengine.audio;
import mlibs.testing;
import mcpplibs.mtest;
import omui.showcase.promo_video;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::fatal;
}

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;
namespace tl = xengine::timeline;

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

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

}  // namespace

UTest cases{"showcase.promo_video", [] {

    "the film reads: two cuts, their lines, the score and the sounds; a mistake is found where it is"_test = [] {
        const promo::Cut main = cut("promo"), short_cut = cut("short");
        expect(eq(main.frames(), std::int64_t{3600}));
        expect(eq(short_cut.frames(), std::int64_t{900}));
        expect(main.picture >= 0 && main.subtitles >= 0 && main.sfx >= 0) << "picture, subtitles and sfx tracks";
        expect(film().subtitles.lines.size() >= 11) << std::format("{} lines", film().subtitles.lines.size());
        auto music = xengine::audio::read_music(*film().files.read("music/promo.music.omui.json"), "promo.music.omui.json");
        expect(music.has_value() && music->tempo == 120) << "the score reads, at 120 a minute";
        auto sounds = xengine::audio::read_sounds(*film().files.read("sounds/promo.sound.omui.json"), "promo.sound.omui.json");
        expect(sounds.has_value() && sounds->size() >= 12) << "the sounds read";
        // What every shot name and clip is: each shot made for its clip.
        promo::Player p(film(), main, {.scale = 0.25f, .fonts = &fonts(), .parallel_raster = false});
        for (std::size_t i = 0; i < main.timeline.tracks[static_cast<std::size_t>(main.picture)].clips.size(); ++i) {
            const auto& c = main.timeline.tracks[static_cast<std::size_t>(main.picture)].clips[i];
            if (c.source == "film") continue;
            expect(p.make(i).has_value()) << c.name;
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
        expect(!a && a.error().front().message.find("no shot is named `stiles`") != std::string::npos) << "an unknown shot";
        auto b = broken("line:effects", "line:affects");
        expect(!b && b.error().front().message.find("no line") != std::string::npos) << "an unknown line";
        auto c = broken("speed = -5", "speed = 5");
        expect(!c && c.error().front().message.find("before itself") != std::string::npos) << "a rewind must play frames before it";
    };

    "the acceptance's data: duration, spec, both languages up long enough to read, every cut and hit on the beat, its onset heard"_test = [] {
        for (std::string_view name : promo::cut_names) {
            const promo::Cut c = cut(name);
            promo::Player p(film(), c, {.scale = 0.25f, .fonts = &fonts(), .parallel_raster = false});
            auto track = promo::soundtrack(film(), c, p);
            expect(track.has_value()) << (track ? "" : track.error());
            if (!track) continue;
            const promo::Acceptance a = promo::check_data(film(), c, *track, {});
            for (const auto& v : a.verdicts) {
                std::println("    {:<6} {:<9} {} {}", name, v.name, v.ok ? "ok" : "FAILS", v.detail);
                expect(v.ok) << std::format("{}: {}: {}", name, v.name, v.detail);
            }
            for (const auto& f : a.findings) expect(false) << std::format("{}: {} (frame {})", f.check, f.what, f.frame);
            // The sound pinned: the same samples everywhere.
            std::println("    {:<6} sound {} ({} cues)", name, hex(xengine::audio::hash(track->samples)), track->cues.size());
            expect(eq(hex(xengine::audio::hash(track->samples)), std::string(name == "promo" ? "ee3b2de561dcdcea" : "4cd8e041d7edf38a")));
        }
    };

    "each shot's timing is declared, hanim's tweens on one timeline: the events' spans too, none left in the scene code"_test = [] {
        const promo::Cut c = cut("promo");
        promo::Player p(film(), c, {.scale = 0.25f, .fonts = &fonts(), .parallel_raster = false});
        const tl::Track& pictures = c.timeline.tracks[static_cast<std::size_t>(c.picture)];
        std::map<std::string, std::vector<std::string>> declared;
        for (const tl::Clip& clip : pictures.clips) {
            if (!clip.source.starts_with("shot:") || clip.length <= 0) continue;
            expect(p.frame(c.timeline.rate.frame_at(clip.end()) - 1).has_value()) << clip.name;
            const promo::Shot* shot = p.scene_shot();
            if (const promo::Choreography* timing = shot ? shot->choreography() : nullptr)
                for (const promo::TimedSpan& span : timing->spans()) {
                    expect(span.to > span.from) << std::format("{}: {} runs forwards", clip.name, span.target);
                    declared[clip.name].push_back(span.target);
                }
        }
        for (const auto* shot : {"origin", "code", "infinite", "agent", "time", "end"})
            expect(!declared[shot].empty()) << std::format("{} declares its timing", shot);
        // The agent's card comes when it asks and goes when the person answers: spans found at their moments.
        const auto& agent = declared["agent"];
        expect(std::ranges::contains(agent, std::string("card.in")) && std::ranges::contains(agent, std::string("card.out"))) << "the card's spans, at its events";
        // No window over a shot's own time is left in the scene code: the timing is the choreography's.
        const auto shots = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "src" / "shots";
        for (const auto& entry : std::filesystem::directory_iterator(shots)) {
            std::ifstream in(entry.path(), std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(in), {}};
            expect(!text.contains("window(t,") && !text.contains("window(s.t,")) << std::format("{} times nothing by hand", entry.path().filename().string());
        }
    };

    "frames pinned on every platform: a frame of each chapter at half size, one at 1080p, one at 4K"_test = [] {
        const promo::Cut c = cut("promo");
        // Each chapter's signature moment: the pixel, the wordmark, the counter in TypeScript and C++, the wall, neon, the needs-answer, the rally, the rewind, the command.
        constexpr std::array<std::int64_t, 10> frames{60, 400, 900, 1500, 1820, 1830 + 30, 2470, 2900, 3030, 3420};
        promo::Player p(film(), c, {.scale = 0.5f, .fonts = &fonts(), .parallel_raster = false});
        Hash all;
        const auto dir = scratch_dir("promo-frames");  // once: it starts empty
        for (const std::int64_t f : frames) {
            auto px = p.frame(f);
            expect(px.has_value()) << (px ? "" : px.error());
            if (!px) return;
            const auto h = promo::pixel_hash(**px);
            all.u64(h);
            std::println("    frame {:>4}: {}", f, hex(h));
            expect(mlibs::image::save_png(dir / std::format("{:04}.png", f), {reinterpret_cast<const std::uint8_t*>((*px)->row(0)), (*px)->width(), (*px)->height(),
                                                                          (*px)->width() * 4, mlibs::image::Alpha::premultiplied})
                      .has_value());
        }
        expect(eq(all.hex(), std::string("68d51c6fdc962699")));
        promo::Player full(film(), c, {.scale = 1, .fonts = &fonts(), .parallel_raster = false});
        auto hd = full.frame(1860);
        expect(hd && (*hd)->width() == 1920 && (*hd)->height() == 1080) << "1080p";
        if (hd) expect(eq(hex(promo::pixel_hash(**hd)), std::string("25318c2baa60810e")));
        promo::Player uhd(film(), c, {.scale = 2, .fonts = &fonts(), .parallel_raster = false});
        auto k4 = uhd.frame(400);
        expect(k4 && (*k4)->width() == 3840 && (*k4)->height() == 2160) << "4K";
        if (k4) expect(eq(hex(promo::pixel_hash(**k4)), std::string("8857e7cc0ed245ac")));
    };

    "seek: a frame drawn directly is the frame played to — in every chapter, after its moments, and through the rewind"_test = [] {
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
                expect(px.has_value()) << (px ? "" : px.error());
                if (!px) return;
                played[f] = promo::pixel_hash(**px);
            }
            for (const std::int64_t f : {a, (a + b) / 2, b - 1}) {
                auto d = p.direct(f);
                expect(d.has_value() && promo::pixel_hash(*d) == played[f]) << std::format("frame {} drawn directly", f);
                ++compared;
            }
        }
        std::println("    {} frames drawn directly, each the frame played to", compared);
    };

    "the film rewinds itself, and plays again: the rewind is its own frames backwards; the replay, the first play's very frames"_test = [] {
        const promo::Cut c = cut("promo");
        const auto r = promo::shown(c, 3020);
        expect(r && r->film && c.timeline.rate.frame_at(r->source) == 2999 - 5 * 20) << "frame 3020 plays back frame 2899";
        // Without the lines and the timeline (they say where in the film it is), the replay is the first play, byte for byte.
        promo::Player p(film(), c, {.scale = 0.25f, .subtitles = false, .timebar = false, .chapters = false, .fonts = &fonts(), .parallel_raster = false});
        std::vector<std::uint64_t> first, again;
        for (std::int64_t k = 0; k < 40; ++k) {
            auto a = p.frame(2760 + k);
            expect(a.has_value());
            if (!a) return;
            first.push_back(promo::pixel_hash(**a));
        }
        for (std::int64_t k = 0; k < 40; ++k) {
            auto b = p.frame(3060 + k);
            expect(b.has_value());
            if (!b) return;
            again.push_back(promo::pixel_hash(**b));
        }
        expect(first == again) << "the replay's frames are the first play's (before its stamp)";
        expect(std::set<std::uint64_t>(first.begin(), first.end()).size() > 30) << "and the game moves";
    };

    "every sampled frame's typography at rest is inside the title-safe area, apart and no smaller than the film's least, and none is blank — 16:9, and the short at 9:16"_test = [] {
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
                expect(px.has_value()) << (px ? "" : px.error());
                if (!px) return;
                for (const auto& finding : promo::check_frame(p, f, **px)) {
                    expect(false) << std::format("{} {}×{} frame {}: {}: {}", run.name, run.size.width, run.size.height, f, finding.check, finding.what);
                    ++problems;
                }
                ++n;
            }
            std::println("    {} at {}: {} frames, {} problems", run.name, run.size.width > 0 ? "9:16" : "16:9", n, problems);
        }
        // The painters' labels too: none under the film's least size.
        expect(promo::Labels::smallest() >= promo::least_type) << std::format("a painter's label at {} px, under the film's least {}", promo::Labels::smallest(), promo::least_type);
    };

    "the README's loop: a few seconds of every chapter — an animated WebP under 1 MB"_test = [] {
        const promo::Cut c = cut("promo");
        promo::Player p(film(), c, {.scale = 0.25f, .fonts = &fonts(), .parallel_raster = false});
        // A beat of each chapter at 7.5 frames a second.
        constexpr std::array<std::pair<std::int64_t, std::int64_t>, 8> beats{{{40, 160}, {330, 450}, {900, 960}, {1260, 1500}, {1740, 2040}, {2400, 2580}, {2880, 3060}, {3360, 3600}}};
        std::vector<mlibs::image::Image> kept;
        for (const auto& [a, b] : beats)
            for (std::int64_t f = a; f < b; f += 8) {
                auto px = p.frame(f);
                expect(px.has_value());
                if (!px) return;
                mlibs::image::Image img;
                img.width = (*px)->width();
                img.height = (*px)->height();
                img.alpha = mlibs::image::Alpha::premultiplied;
                img.rgba.assign(reinterpret_cast<const std::uint8_t*>((*px)->row(0)),
                                reinterpret_cast<const std::uint8_t*>((*px)->row(0)) + static_cast<std::size_t>(img.width) * img.height * 4);
                kept.push_back(std::move(img));
            }
        std::vector<mlibs::image::View> views;
        for (const auto& k : kept) views.push_back(mlibs::image::view(k));
        const auto file = scratch_dir("readme-promo-video") / "film.webp";
        expect(mlibs::image::save_animated_webp(file, views, {.delay_num = 2, .delay_den = 15, .quality = 72}).has_value());
        const auto size = std::filesystem::file_size(file);
        std::println("    the loop: {} frames, {} KB", views.size(), size / 1024);
        expect(size < 1024 * 1024) << std::format("{} bytes", size);
    };

    "what a frame depends on (D-54's frame cache): an edit changes exactly the frames that read it"_test = [] {
        const promo::Cut c = cut("short");
        const promo::PlayerOptions o;
        promo::Digests a(film(), c, o, 1), again(film(), c, o, 1), other_program(film(), c, o, 2);
        expect(a.frame(300) == again.frame(300) && a.frames(0, c.frames()) == again.frames(0, c.frames())) << "the same inputs, the same digests";
        expect(a.frame(300) != other_program.frame(300)) << "another program: every frame another";
        // One line edited, in a copy of the files: the frames it is up on change, no other.
        const auto dir = scratch_dir("promo-digests");
        auto text = film().files.read("promo.subtitles.omui.toml");
        expect(text.has_value());
        if (!text) return;
        const std::string from = "A thousand windows, one unbroken shot", to = "A thousand windows in one unbroken shot";
        const auto at = text->find(from);
        expect(at != std::string::npos) << "the line to edit";
        if (at == std::string::npos) return;
        std::string edited = *text;
        edited.replace(at, from.size(), to);
        std::ofstream(dir / "promo.subtitles.omui.toml", std::ios::binary) << edited;
        auto changed_film = promo::load_film(promo::Files(dir));
        expect(changed_film.has_value());
        if (!changed_film) return;
        auto changed_cut = promo::load_cut(changed_film->files, "short", changed_film->subtitles);
        expect(changed_cut.has_value());
        if (!changed_cut) return;
        promo::Digests b(*changed_film, *changed_cut, o, 1);
        std::int64_t differ = 0, wrong = 0;
        for (std::int64_t i = 0; i < c.frames(); ++i) {
            bool up = false;
            for (const auto& l : promo::lines_at(c, film().subtitles, i)) up = up || (l.line && l.line->en == from);
            const bool changed = a.frame(i) != b.frame(i);
            differ += changed;
            wrong += changed != up;
        }
        std::println("    {} of {} frames changed by the line", differ, c.frames());
        expect(differ > 0 && wrong == 0) << std::format("exactly the frames the line is up on ({} wrong)", wrong);
    };
}};
