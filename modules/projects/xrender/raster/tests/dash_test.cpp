// Public coverage, not a second implementation of the dash walker.
import std;
import mlibs.geom;
import xrender.raster;
import mlibs.testing;

using namespace mlibs::testing;
namespace raster = xrender::raster;

namespace {
raster::Coverage line(std::span<const float> pattern = {}, float offset = 0) {
    raster::PathBuilder path;
    path.move_to({2, 8}).line_to({122, 8});
    raster::Coverage out;
    raster::stroke_coverage(path.data(), {.width = 2, .dashes = pattern, .dash_offset = offset}, {}, {0, 0, 128, 16}, out);
    return out;
}
bool same(const raster::Coverage& a, const raster::Coverage& b) {
    return a.bounds.x == b.bounds.x && a.bounds.y == b.bounds.y && a.bounds.width == b.bounds.width &&
        a.bounds.height == b.bounds.height && a.values == b.values;
}
}

int main(int argc, char** argv) {
    Suite suite{"raster.dash"};
    suite.test("odd patterns equal their explicitly doubled even pattern at every phase", [] {
        const std::array odd{4.0f, 2.0f, 1.0f};
        const std::array even{4.0f, 2.0f, 1.0f, 4.0f, 2.0f, 1.0f};
        for (float phase : {0.0f, 1.0f, 6.0f, 7.0f, 8.0f, 13.0f, 14.0f, -1.0f, -7.0f, -15.0f})
            check(same(line(odd, phase), line(even, phase)), std::format("same real coverage at phase {}", phase));
        check(!same(line(odd), line(odd, 7)), "one traversal of an odd list reverses on/off; it is not the complete period");
    });
    suite.test("ordinary offset wrapping preserves actual dash coverage", [] {
        const std::array pattern{6.0f, 5.0f};
        check(same(line(pattern, 2), line(pattern, 24)));
        check(same(line(pattern, -2), line(pattern, 9)));
        check(!same(line(pattern, 0), line(pattern, 2)));
    });
    suite.test("an all-zero pattern keeps the existing solid stroke fallback", [] {
        const std::array zeros{0.0f, 0.0f, 0.0f};
        check(same(line(zeros), line()));
    });
    suite.test("invalid negative and nonfinite dash data cannot trap a frame", [] {
        const std::array negative{-1.0f, 1.0f};
        const std::array nan{std::numeric_limits<float>::quiet_NaN(), 1.0f};
        const std::array infinity{std::numeric_limits<float>::infinity(), 1.0f};
        for (const auto& pattern : {negative, nan, infinity})
            check(same(line(pattern), line()), "invalid dashing uses the documented solid fallback");
        const std::array valid{6.0f, 5.0f};
        for (float offset : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
            check(same(line(valid, offset), line(valid)), "a nonfinite offset uses zero phase");
    });
    suite.test("huge finite offsets reduce exactly without a quotient or an unbounded phase walk", [] {
        const std::array pattern{6.0f, 5.0f};
        // FLT_MAX = (2^24-1)*2^104. Its exact integer remainder modulo 11
        // is 9; the corresponding negative offset has normalized phase 2.
        check(same(line(pattern, std::numeric_limits<float>::max()), line(pattern, 9)));
        check(same(line(pattern, -std::numeric_limits<float>::max()), line(pattern, 2)));
    });
    return suite.run(argc, argv);
}
