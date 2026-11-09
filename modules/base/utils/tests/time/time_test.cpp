// mlibs.time: every common frame rate and sample rate is a whole number of
// flicks; a frame's start is exact in flicks and the same double in seconds;
// a time maps to the frame it falls in, before zero too; seconds and flicks
// convert to the nearest.
import std;
import mlibs.time;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace mt = mlibs::time;

UTest cases{"time", [] {

    "the rates a timeline may have are exact, in flicks"_test = [] {
        for (const mt::Rate r : {mt::Rate{24, 1}, {25, 1}, {30, 1}, {48, 1}, {50, 1}, {60, 1}, {90, 1}, {120, 1}, {24000, 1001}, {30000, 1001}, {60000, 1001}})
            expect(r.exact()) << std::format("{}/{}", r.num, r.den);
        for (const std::int64_t hz : {22050, 44100, 48000, 88200, 96000}) expect(mt::exact_samples(hz)) << std::format("{} Hz", hz);
        expect(!mt::Rate{11, 1}.exact() && !mt::exact_samples(11)) << "a second is not a whole number of elevenths of it in flicks";
        expect(!mt::Rate{0, 1}.exact() && !mt::Rate{60, 0}.exact()) << "no rate is exact without a numerator and a denominator";
    };

    "a frame's start in flicks is exact, and maps back"_test = [] {
        constexpr mt::Rate ntsc{30000, 1001};
        static_assert(ntsc.frame_time(30000) == mt::Flicks{1001} * mt::flicks_per_second);  // 30000 frames are 1001 seconds
        constexpr mt::Rate sixty{60, 1};
        expect(eq(sixty.frame_length(), mt::flicks_per_second / 60));
        expect(eq(sixty.frame_at(sixty.frame_time(37)), std::int64_t{37}));
        expect(eq(sixty.frame_at(sixty.frame_time(37) + 1), std::int64_t{37}));
        expect(eq(sixty.frame_at(sixty.frame_time(38) - 1), std::int64_t{37}));
        expect(eq(sixty.frame_at(mt::Flicks{-1}), std::int64_t{-1}));
        expect(eq(sixty.frame_at(-sixty.frame_length()), std::int64_t{-1}));
        expect(eq(mt::sample_at(48000, sixty.frame_time(1)), std::int64_t{800}));
        expect(eq(mt::sample_time(48000, 800), sixty.frame_time(1)));
        expect(eq(mt::sample_at(48000, mt::Flicks{-1}), std::int64_t{-1}));
    };

    "a frame's start in seconds is one rounding, and maps back"_test = [] {
        constexpr mt::Rate ntsc{30000, 1001};
        for (const std::int64_t f : {0LL, 1LL, 29LL, 30LL, 1799LL, 107892LL, -1LL, -30LL}) expect(eq(ntsc.frame_at(ntsc.time_of(f)), f));
        constexpr mt::Rate sixty{60, 1};
        expect(eq(std::bit_cast<std::uint64_t>(double(sixty.time_of(30).count())), std::bit_cast<std::uint64_t>(double(0.5))));
        expect(eq(sixty.frame_at(mt::seconds(0.5 - 1e-12)), std::int64_t{29}));
        expect(eq(std::bit_cast<std::uint64_t>(double(ntsc.frame_duration().count())), std::bit_cast<std::uint64_t>(double(1001.0 / 30000.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(ntsc.fps())), std::bit_cast<std::uint64_t>(double(30000.0 / 1001.0))));
        // The two agree: a frame's start in flicks, as seconds, is its start in seconds.
        for (const std::int64_t f : {0LL, 7LL, 1799LL}) expect(near(mt::to_seconds(ntsc.frame_time(f)), ntsc.time_of(f).count(), 1e-12));
    };

    "seconds and flicks convert to the nearest"_test = [] {
        expect(eq(mt::from_seconds(1.5), mt::flicks_per_second * 3 / 2));
        expect(eq(mt::from_seconds(-0.25), -mt::flicks_per_second / 4));
        expect(eq(mt::from_seconds(1.0 / 705'600'000 * 0.6), mt::Flicks{1}));
        expect(eq(std::bit_cast<std::uint64_t>(double(mt::to_seconds(mt::flicks_per_second / 4))), std::bit_cast<std::uint64_t>(double(0.25))));
        for (const mt::Flicks t : {mt::Flicks{0}, mt::Flicks{1}, mt::flicks_per_second / 7, -mt::flicks_per_second * 61 / 60})
            expect(eq(mt::from_seconds(mt::to_seconds(t)), t));
    };

    "rates compare by their ratio's terms"_test = [] {
        expect(mt::Rate{60, 1} == mt::Rate{}) << "60/1 is the default";
        expect(!(mt::Rate{60, 1} == mt::Rate{120, 2})) << "terms, not value: a file writes what it read";
    };
}};
