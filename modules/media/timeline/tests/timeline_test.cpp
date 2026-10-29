// openmui.timeline: exact time, what plays when, edits and their inverses, the file.
import std;
import hanim;
import openmui.timeline;
import openmui.testing;

using namespace omui::testing;
namespace tl = omui::timeline;

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

int main(int argc, char** argv) {
    Suite s{"timeline"};

    s.test("time is exact: every common frame rate and sample rate is a whole number of flicks; frames and samples found from a time", [] {
        for (const tl::Rate r : {tl::Rate{24, 1}, {25, 1}, {30, 1}, {48, 1}, {50, 1}, {60, 1}, {90, 1}, {120, 1}, {24000, 1001}, {30000, 1001}, {60000, 1001}})
            check(tl::exact(r), std::format("{}/{}", r.num, r.den));
        for (const std::int32_t hz : {44100, 48000, 88200, 96000, 22050}) check(tl::exact_samples(hz), std::format("{} Hz", hz));
        check(!tl::exact(tl::Rate{7, 3}) || tl::flicks_per_second * 3 % 7 == 0, "a rate that does not divide is said to");
        const tl::Rate ntsc{30000, 1001};
        check_eq(tl::frame_time(ntsc, 30000), tl::Flicks{1001} * tl::flicks_per_second);  // 30000 frames are exactly 1001 seconds
        check_eq(tl::frame_at(tl::Rate{60, 1}, tl::frame_time(tl::Rate{60, 1}, 37) + 1), std::int64_t{37});
        check_eq(tl::frame_at(tl::Rate{60, 1}, -1), std::int64_t{-1});
        check_eq(tl::sample_at(48000, tl::frame_time(tl::Rate{60, 1}, 1)), std::int64_t{800});
    });

    s.test("times as text: frames, seconds, milliseconds and flicks, exactly — and written back the shortest exact way", [] {
        const tl::Rate r{60, 1};
        check_eq(tl::parse_time("90f", r), std::optional<tl::Flicks>(tl::frame_time(r, 90)));
        check_eq(tl::parse_time("1.5s", r), std::optional<tl::Flicks>(tl::flicks_per_second * 3 / 2));
        check_eq(tl::parse_time("250ms", r), std::optional<tl::Flicks>(tl::flicks_per_second / 4));
        check_eq(tl::parse_time("-2s", r), std::optional<tl::Flicks>(-2 * tl::flicks_per_second));
        check_eq(tl::parse_time("17fl", r), std::optional<tl::Flicks>(17));
        check(!tl::parse_time("0.0000000001s", r), "between flicks: not a time");
        check(!tl::parse_time("1.5f", r) && !tl::parse_time("s", r) && !tl::parse_time("ten", r), "not times");
        check_eq(tl::time_text(tl::frame_time(r, 90), r), std::string("90f"));
        check_eq(tl::time_text(tl::flicks_per_second / 4 + tl::frame_time(r, 0) + 1, r), std::string("176400001fl"));
        check_eq(tl::time_text(tl::flicks_per_second / 1000, r), std::string("0.001s"));
        for (const tl::Flicks t : {tl::Flicks{0}, tl::Flicks{1}, tl::flicks_per_second / 7, tl::flicks_per_second * 61 / 60, -tl::flicks_per_second / 3})
            check_eq(tl::parse_time(tl::time_text(t, r), r), std::optional<tl::Flicks>(t));
    });

    s.test("a timeline file reads; what plays at a moment — which clips, where in their sources, at what level and with which values", [] {
        auto t = tl::read_timeline(promo, "promo.timeline.omui.toml");
        check(t.has_value(), t ? "" : t.error().front().text());
        if (!t) return;
        check(t->tracks.size() == 2 && t->tracks[0].clips.size() == 2 && t->marker("beat") == std::optional<tl::Flicks>(2 * tl::flicks_per_second), "read");
        check_eq(t->duration(), tl::Flicks{5} * tl::flicks_per_second);
        std::vector<tl::Playing> now;
        const tl::Rate r = t->rate;
        tl::playing(*t, tl::frame_time(r, 0), now);
        check(now.size() == 2 && now[0].level == 0 && now[1].level == 0.8f, "the title fading in from 0 at its first frame, the theme at its level");
        tl::playing(*t, tl::frame_time(r, 30), now);
        check(now.size() == 2 && now[0].level == 1, "faded in by 15f");
        const tl::Clip& title = t->tracks[0].clips[0];
        check_eq(title.value("y", tl::frame_time(r, 0)), 40.0);
        check_eq(title.value("y", tl::frame_time(r, 30)), 0.0);
        check(title.value("y", tl::frame_time(r, 15)) < 10, "eased out: most of the way by half the time");
        check_eq(title.value("missing", 0, -1.0), -1.0);
        // The fight at half speed from 10s into its source: one second in, half a second further.
        tl::playing(*t, tl::Flicks{4} * tl::flicks_per_second, now);
        check(now.size() == 2 && now[0].clip == 1 && now[0].source == tl::flicks_per_second * 21 / 2, "half speed from 10s");
        tl::playing(*t, tl::Flicks{6} * tl::flicks_per_second, now);
        check(now.empty(), "after everything: nothing");
        // Written, read back, written again: the same text.
        const std::string written = all(*t);
        auto again = tl::read_timeline(written, "written.timeline.omui.toml");
        check(again && all(*again) == written, written);
    });

    s.test("a file's problems at their line and column, each with the nearest right word; a timeline that breaks its rules is refused", [] {
        auto bad = tl::read_timeline("[timeline]\nrate = 11\nnmae = \"x\"\n\n[[track]]\nkind = \"vidoe\"\n[[track.clip]]\nat = \"1.5f\"\n", "bad.timeline.omui.toml");
        check(!bad.has_value());
        if (bad) return;
        std::vector<std::string> texts;
        for (const auto& p : bad.error()) texts.push_back(p.text());
        auto has = [&](std::string_view part) { return std::ranges::any_of(texts, [&](const std::string& x) { return x.contains(part); }); };
        check(has("bad.timeline.omui.toml:3:1: [timeline] has no key `nmae` (did you mean `name`?)"), std::format("{}", texts));
        check(has("bad.timeline.omui.toml:6:8: `vidoe` is not a kind of track (did you mean `video`?)"), std::format("{}", texts));
        check(has("is not a whole number of flicks"), std::format("{}", texts));
        check(has("`1.5f` is not a clip's `at`"), std::format("{}", texts));
        auto overlap = tl::read_timeline("[[track]]\nname = \"a\"\n[[track.clip]]\nname = \"x\"\nlength = \"2s\"\n[[track.clip]]\nname = \"y\"\nat = \"1s\"\nlength = \"1s\"\n", "o.timeline.omui.toml");
        check(!overlap && overlap.error().front().message.contains("overlaps `x`"), overlap ? "accepted" : overlap.error().front().text());
    });

    s.test("edits: insert, move, trim, split, ripple — each applied gives back its inverse, which restores the timeline exactly; a change that cannot be made is not made at all", [] {
        auto t = tl::read_timeline(promo, "promo.timeline.omui.toml");
        if (!t) return;
        const tl::Rate r = t->rate;
        const std::string original = all(*t);
        auto round_trip = [&](std::expected<tl::Change, std::string> change, std::string_view what) {
            check(change.has_value(), change ? "" : change.error());
            if (!change) return;
            const std::string before = all(*t);
            auto undo = tl::apply(*t, *change);
            check(undo.has_value(), undo ? "" : undo.error());
            if (!undo) return;
            check(tl::check(*t).empty(), std::format("{}: the rules kept", what));
            const std::string after = all(*t);
            check(after != before, std::format("{}: something changed", what));
            auto redo = tl::apply(*t, *undo);
            check(redo && all(*t) == before, std::format("{}: undone exactly", what));
            if (redo) check(tl::apply(*t, *redo) && all(*t) == after, std::format("{}: redone exactly", what));
            if (auto back = tl::apply(*t, *undo)) check(all(*t) == before, "and undone again");  // leave it as it was
        };
        round_trip(tl::insert(*t, 0, tl::Clip{.name = "logo", .at = 6 * tl::flicks_per_second, .length = tl::frame_time(r, 30)}), "insert");
        round_trip(tl::move(*t, 0, 0, 0, 5 * tl::flicks_per_second), "move later on its track");
        round_trip(tl::trim(*t, 0, 1, 4 * tl::flicks_per_second, tl::frame_time(r, 60)), "trim the fight's start");
        round_trip(tl::split(*t, 0, 0, tl::flicks_per_second), "split the title");
        round_trip(tl::ripple_remove(*t, 0, 0), "ripple remove the title");
        check_eq(all(*t), original);
        // Split: the second half goes on where the first stopped (its source, its keys).
        {
            auto change = tl::split(*t, 0, 1, 4 * tl::flicks_per_second);
            auto undo = change ? tl::apply(*t, *change) : std::unexpected(change.error());
            check(undo.has_value(), undo ? "" : undo.error());
            const tl::Clip& a = t->tracks[0].clips[1];
            const tl::Clip& b = t->tracks[0].clips[2];
            check(a.end() == b.at && b.in == a.source_at(b.at), "the second half from where the first stopped");
            if (undo) (void)tl::apply(*t, *undo);
        }
        {
            auto change = tl::split(*t, 0, 0, tl::frame_time(r, 20));
            auto undo = change ? tl::apply(*t, *change) : std::unexpected(change.error());
            const tl::Clip& b = t->tracks[0].clips[1];
            check_eq(b.value("y", b.at + tl::frame_time(r, 10)), t->tracks[0].clips[0].value("y", tl::frame_time(r, 30)));  // the keys go on
            if (undo) (void)tl::apply(*t, *undo);
        }
        // Not made at all: moved onto the fight, or onto a track of another kind; the timeline as it was.
        const std::string before = all(*t);
        auto onto = tl::move(*t, 0, 0, 0, 3 * tl::flicks_per_second + 1);
        check(onto && !tl::apply(*t, *onto) && all(*t) == before, "a move onto another clip refused, nothing changed");
        check(!tl::move(*t, 0, 0, 1, 0), "a picture onto the music track refused");
        check(!tl::split(*t, 0, 0, 0), "a split at a clip's start refused");
    });

    s.test("what plays, a minute of frames into one reused list: the same answers on every platform (their hash)", [] {
        auto t = tl::read_timeline(promo, "promo.timeline.omui.toml");
        if (!t) return;
        std::vector<tl::Playing> now;
        now.reserve(8);
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        for (std::int64_t f = 0; f < 3600; ++f) {
            const tl::Flicks at = tl::frame_time(t->rate, f);
            tl::playing(*t, at, now);
            for (const tl::Playing& p : now) {
                mix(p.track), mix(p.clip), mix(static_cast<std::uint64_t>(p.source)), mix(std::bit_cast<std::uint32_t>(p.level));
                mix(std::bit_cast<std::uint64_t>(t->tracks[p.track].clips[p.clip].value("y", at)));
            }
        }
        std::println("    timeline playing {:016x}", h);
        check_eq(h, std::uint64_t{0xc00cf3d87babbff7});  // GCC, LLVM and wasm
    });

    return s.run(argc, argv);
}
