// xengine.timeline: exact time, what plays when, edits and their inverses, the file.
import std;
import hanim.core;
import xengine.timeline;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace tl = xengine::timeline;

namespace {

constexpr std::string_view promo = R"(
[timeline]
name = "promo"
rate = 60
sample_rate = 48000
size = [1920, 1080]

[[marker]]
at = "2s"
name = "beat"

[[track]]
name = "picture"
kind = "video"

[[track.clip]]
name = "title"
source = "views/title.view.omui.json"
at = "0f"
length = "3s"
fade = { in = "15f", out = "0.25s", ease = "out_cubic" }

[[track.clip.property]]
name = "y"
keys = [["0f", 40, "out_cubic"], ["30f", 0], ["2s", 0, "hold"]]

[[track.clip]]
name = "fight"
source = "showcase/fighting"
at = "3s"
length = "120f"
in = "10s"
speed = 0.5

[[track]]
name = "music"
kind = "audio"

[[track.clip]]
name = "theme"
source = "menu.music.omui.toml"
at = "0s"
length = "5s"
level = 0.8
)";

std::string all(const tl::Timeline& t) { return tl::write_timeline(t); }

}  // namespace

UTest cases{"timeline", [] {

    "time is exact: every common frame rate and sample rate is a whole number of flicks; frames and samples found from a time"_test = [] {
        for (const mlibs::time::Rate r : {mlibs::time::Rate{24, 1}, {25, 1}, {30, 1}, {48, 1}, {50, 1}, {60, 1}, {90, 1}, {120, 1}, {24000, 1001}, {30000, 1001}, {60000, 1001}})
            expect(r.exact()) << std::format("{}/{}", r.num, r.den);
        for (const std::int32_t hz : {44100, 48000, 88200, 96000, 22050}) expect(mlibs::time::exact_samples(hz)) << std::format("{} Hz", hz);
        expect(!mlibs::time::Rate{7, 3}.exact() || mlibs::time::flicks_per_second * 3 % 7 == 0) << "a rate that does not divide is said to";
        const mlibs::time::Rate ntsc{30000, 1001};
        expect(eq(ntsc.frame_time(30000), mlibs::time::Flicks{1001} * mlibs::time::flicks_per_second));  // 30000 frames are exactly 1001 seconds
        expect(eq(mlibs::time::Rate{60, 1}.frame_at(mlibs::time::Rate{60, 1}.frame_time(37) + 1), std::int64_t{37}));
        expect(eq(mlibs::time::Rate{60, 1}.frame_at(-1), std::int64_t{-1}));
        expect(eq(mlibs::time::sample_at(48000, mlibs::time::Rate{60, 1}.frame_time(1)), std::int64_t{800}));
    };

    "times as text: frames, seconds, milliseconds and flicks, exactly — and written back the shortest exact way"_test = [] {
        const mlibs::time::Rate r{60, 1};
        expect(eq(tl::parse_time("90f", r), std::optional<mlibs::time::Flicks>(r.frame_time(90))));
        expect(eq(tl::parse_time("1.5s", r), std::optional<mlibs::time::Flicks>(mlibs::time::flicks_per_second * 3 / 2)));
        expect(eq(tl::parse_time("250ms", r), std::optional<mlibs::time::Flicks>(mlibs::time::flicks_per_second / 4)));
        expect(eq(tl::parse_time("-2s", r), std::optional<mlibs::time::Flicks>(-2 * mlibs::time::flicks_per_second)));
        expect(eq(tl::parse_time("17fl", r), std::optional<mlibs::time::Flicks>(17)));
        expect(!tl::parse_time("0.0000000001s", r)) << "between flicks: not a time";
        expect(!tl::parse_time("1.5f", r) && !tl::parse_time("s", r) && !tl::parse_time("ten", r)) << "not times";
        expect(eq(tl::time_text(r.frame_time(90), r), std::string("90f")));
        expect(eq(tl::time_text(mlibs::time::flicks_per_second / 4 + r.frame_time(0) + 1, r), std::string("176400001fl")));
        expect(eq(tl::time_text(mlibs::time::flicks_per_second / 1000, r), std::string("0.001s")));
        for (const mlibs::time::Flicks t : {mlibs::time::Flicks{0}, mlibs::time::Flicks{1}, mlibs::time::flicks_per_second / 7, mlibs::time::flicks_per_second * 61 / 60, -mlibs::time::flicks_per_second / 3})
            expect(eq(tl::parse_time(tl::time_text(t, r), r), std::optional<mlibs::time::Flicks>(t)));
    };

    "a timeline file reads; what plays at a moment — which clips, where in their sources, at what level and with which values"_test = [] {
        auto t = tl::read_timeline(promo, "promo.timeline.omui.toml");
        expect(t.has_value()) << (t ? "" : t.error().front().text());
        if (!t) return;
        expect(t->tracks.size() == 2 && t->tracks[0].clips.size() == 2 && t->marker("beat") == std::optional<mlibs::time::Flicks>(2 * mlibs::time::flicks_per_second)) << "read";
        expect(eq(t->duration(), mlibs::time::Flicks{5} * mlibs::time::flicks_per_second));
        std::vector<tl::Playing> now;
        const mlibs::time::Rate r = t->rate;
        tl::playing(*t, r.frame_time(0), now);
        expect(now.size() == 2 && now[0].level == 0 && now[1].level == 0.8f) << "the title fading in from 0 at its first frame, the theme at its level";
        tl::playing(*t, r.frame_time(30), now);
        expect(now.size() == 2 && now[0].level == 1) << "faded in by 15f";
        const tl::Clip& title = t->tracks[0].clips[0];
        expect(eq(title.value("y", r.frame_time(0)), 40.0));
        expect(eq(title.value("y", r.frame_time(30)), 0.0));
        expect(title.value("y", r.frame_time(15)) < 10) << "eased out: most of the way by half the time";
        expect(eq(title.value("missing", 0, -1.0), -1.0));
        // The fight at half speed from 10s into its source: one second in, half a second further.
        tl::playing(*t, mlibs::time::Flicks{4} * mlibs::time::flicks_per_second, now);
        expect(now.size() == 2 && now[0].clip == 1 && now[0].source == mlibs::time::flicks_per_second * 21 / 2) << "half speed from 10s";
        tl::playing(*t, mlibs::time::Flicks{6} * mlibs::time::flicks_per_second, now);
        expect(now.empty()) << "after everything: nothing";
        // Written, read back, written again: the same text.
        const std::string written = all(*t);
        auto again = tl::read_timeline(written, "written.timeline.omui.toml");
        expect(again && all(*again) == written) << written;
    };

    "a file's problems at their line and column, each with the nearest right word; a timeline that breaks its rules is refused"_test = [] {
        auto bad = tl::read_timeline("[timeline]\nrate = 11\nnmae = \"x\"\n\n[[track]]\nkind = \"vidoe\"\n[[track.clip]]\nat = \"1.5f\"\n", "bad.timeline.omui.toml");
        expect(!bad.has_value());
        if (bad) return;
        std::vector<std::string> texts;
        for (const auto& p : bad.error()) texts.push_back(p.text());
        auto has = [&](std::string_view part) { return std::ranges::any_of(texts, [&](const std::string& x) { return x.contains(part); }); };
        expect(has("bad.timeline.omui.toml:3:1: [timeline] has no key `nmae` (did you mean `name`?)")) << std::format("{}", texts);
        expect(has("bad.timeline.omui.toml:6:8: `vidoe` is not a kind of track (did you mean `video`?)")) << std::format("{}", texts);
        expect(has("is not a whole number of flicks")) << std::format("{}", texts);
        expect(has("`1.5f` is not a clip's `at`")) << std::format("{}", texts);
        auto overlap = tl::read_timeline("[[track]]\nname = \"a\"\n[[track.clip]]\nname = \"x\"\nlength = \"2s\"\n[[track.clip]]\nname = \"y\"\nat = \"1s\"\nlength = \"1s\"\n", "o.timeline.omui.toml");
        expect(!overlap && overlap.error().front().message.contains("overlaps `x`")) << (overlap ? "accepted" : overlap.error().front().text());
    };

    "edits: insert, move, trim, split, ripple — each applied gives back its inverse, which restores the timeline exactly; a change that cannot be made is not made at all"_test = [] {
        auto t = tl::read_timeline(promo, "promo.timeline.omui.toml");
        if (!t) return;
        const mlibs::time::Rate r = t->rate;
        const std::string original = all(*t);
        auto round_trip = [&](std::expected<tl::Change, std::string> change, std::string_view what) {
            expect(change.has_value()) << (change ? "" : change.error());
            if (!change) return;
            const std::string before = all(*t);
            auto undo = tl::apply(*t, *change);
            expect(undo.has_value()) << (undo ? "" : undo.error());
            if (!undo) return;
            expect(tl::check(*t).empty()) << std::format("{}: the rules kept", what);
            const std::string after = all(*t);
            expect(after != before) << std::format("{}: something changed", what);
            auto redo = tl::apply(*t, *undo);
            expect(redo && all(*t) == before) << std::format("{}: undone exactly", what);
            if (redo) expect(tl::apply(*t, *redo) && all(*t) == after) << std::format("{}: redone exactly", what);
            if (auto back = tl::apply(*t, *undo)) expect(all(*t) == before) << "and undone again";  // leave it as it was
        };
        round_trip(tl::insert(*t, 0, tl::Clip{.name = "logo", .at = 6 * mlibs::time::flicks_per_second, .length = r.frame_time(30)}), "insert");
        round_trip(tl::move(*t, 0, 0, 0, 5 * mlibs::time::flicks_per_second), "move later on its track");
        round_trip(tl::trim(*t, 0, 1, 4 * mlibs::time::flicks_per_second, r.frame_time(60)), "trim the fight's start");
        round_trip(tl::split(*t, 0, 0, mlibs::time::flicks_per_second), "split the title");
        round_trip(tl::ripple_remove(*t, 0, 0), "ripple remove the title");
        expect(eq(all(*t), original));
        // Split: the second half goes on where the first stopped (its source, its keys).
        {
            auto change = tl::split(*t, 0, 1, 4 * mlibs::time::flicks_per_second);
            auto undo = change ? tl::apply(*t, *change) : std::unexpected(change.error());
            expect(undo.has_value()) << (undo ? "" : undo.error());
            const tl::Clip& a = t->tracks[0].clips[1];
            const tl::Clip& b = t->tracks[0].clips[2];
            expect(a.end() == b.at && b.in == a.source_at(b.at)) << "the second half from where the first stopped";
            if (undo) (void)tl::apply(*t, *undo);
        }
        {
            auto change = tl::split(*t, 0, 0, r.frame_time(20));
            auto undo = change ? tl::apply(*t, *change) : std::unexpected(change.error());
            const tl::Clip& b = t->tracks[0].clips[1];
            expect(eq(b.value("y", b.at + r.frame_time(10)), t->tracks[0].clips[0].value("y", r.frame_time(30))));  // the keys go on
            if (undo) (void)tl::apply(*t, *undo);
        }
        // Not made at all: moved onto the fight, or onto a track of another kind; the timeline as it was.
        const std::string before = all(*t);
        auto onto = tl::move(*t, 0, 0, 0, 3 * mlibs::time::flicks_per_second + 1);
        expect(onto && !tl::apply(*t, *onto) && all(*t) == before) << "a move onto another clip refused, nothing changed";
        expect(!tl::move(*t, 0, 0, 1, 0)) << "a picture onto the music track refused";
        expect(!tl::split(*t, 0, 0, 0)) << "a split at a clip's start refused";
    };

    "what plays, a minute of frames into one reused list: the same answers on every platform (their hash)"_test = [] {
        auto t = tl::read_timeline(promo, "promo.timeline.omui.toml");
        if (!t) return;
        std::vector<tl::Playing> now;
        now.reserve(8);
        Hash f;
        auto mix = [&](std::uint64_t v) { f.word(v); };
        for (std::int64_t f = 0; f < 3600; ++f) {
            const mlibs::time::Flicks at = t->rate.frame_time(f);
            tl::playing(*t, at, now);
            for (const tl::Playing& p : now) {
                mix(p.track), mix(p.clip), mix(static_cast<std::uint64_t>(p.source)), mix(std::bit_cast<std::uint32_t>(p.level));
                mix(std::bit_cast<std::uint64_t>(t->tracks[p.track].clips[p.clip].value("y", at)));
            }
        }
        const std::uint64_t h = f.value();
        std::println("    timeline playing {:016x}", h);
        expect(eq(h, std::uint64_t{0x5a03a531488aa519}));  // GCC, LLVM and wasm
    };
}};
