import std;
import mlibs.math;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace det = mlibs::math::det;

UTest cases{"math.remainder", [] {
    "truncating remainder has exact values and dividend sign"_test = [] {
        expect(eq(det::fmod(27.5, 4), 3.5));
        expect(eq(det::fmod(-27.5, 4), -3.5));
        expect(eq(det::fmod(27.5, -4), 3.5));
        expect(eq(det::fmod(-27.5, -4), -3.5));
        expect(eq(det::fmod(0x1.0000000000001p+0, 1), 0x1p-52));
        expect(eq(std::bit_cast<std::uint64_t>(det::fmod(-8, 4)), std::uint64_t{0x8000000000000000}));
    };
    "extreme ratios and subnormal phases are exact"_test = [] {
        // DBL_MAX = (2^53-1)*2^971, whose integer remainder modulo 11 is 3.
        expect(eq(det::fmod(std::numeric_limits<double>::max(), 11), 3.0));
        expect(eq(det::fmod(-std::numeric_limits<double>::max(), 11), -3.0));
        const auto tiny = std::numeric_limits<double>::denorm_min();
        expect(eq(det::fmod(99 * tiny, 7 * tiny), tiny));
        expect(eq(det::fmod(std::numeric_limits<double>::min(), 3 * tiny), tiny));
    };
    "special results have canonical NaN bits and preserve finite signed zero"_test = [] {
        const auto inf = std::numeric_limits<double>::infinity();
        const auto nan = std::bit_cast<double>(std::uint64_t{0xfff8000000000123});
        for (const auto [x, y] : std::array{std::pair{inf, 1.0}, std::pair{nan, 1.0},
                std::pair{1.0, nan}, std::pair{1.0, 0.0}, std::pair{-1.0, -0.0}})
            expect(eq(std::bit_cast<std::uint64_t>(det::fmod(x, y)), std::uint64_t{0x7ff8000000000000}));
        expect(eq(det::fmod(12.5, inf), 12.5));
        expect(eq(det::fmod(-12.5, -inf), -12.5));
        expect(eq(std::bit_cast<std::uint64_t>(det::fmod(-0.0, inf)), std::uint64_t{0x8000000000000000}));
    };
}};
