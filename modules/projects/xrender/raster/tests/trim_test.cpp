// Trimmed strokes: the drawn part of each contour, as fractions of its
// length, equals stroking the same stretch drawn by hand.
import std;
import mlibs.geom;
import xrender.raster;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace raster = xrender::raster;

namespace {
constexpr raster::PixelRect area{0, 0, 128, 64};

raster::Coverage stroked(const raster::PathBuilder& path, raster::Stroke stroke) {
    raster::Coverage out;
    raster::stroke_coverage(path.data(), stroke, {}, area, out);
    return out;
}
raster::Coverage trimmed(const raster::PathBuilder& path, float start, float end,
                         float offset = 0) {
    return stroked(path, {.width = 2, .trim_start = start, .trim_end = end, .trim_offset = offset});
}
raster::Coverage plain(const raster::PathBuilder& path) {
    return stroked(path, {.width = 2});
}
bool same(const raster::Coverage& left, const raster::Coverage& right) {
    return left.bounds.x == right.bounds.x && left.bounds.y == right.bounds.y &&
           left.bounds.width == right.bounds.width && left.bounds.height == right.bounds.height &&
           left.values == right.values;
}
bool empty(const raster::Coverage& coverage) {
    return std::ranges::all_of(coverage.values, [](auto value) { return value == 0; });
}
raster::PathBuilder segment(raster::Point from, raster::Point to) {
    raster::PathBuilder path;
    path.move_to(from).line_to(to);
    return path;
}
raster::PathBuilder square() {  // perimeter 160, starting at its top-left corner
    raster::PathBuilder path;
    path.move_to({10, 10}).line_to({50, 10}).line_to({50, 50}).line_to({10, 50}).close();
    return path;
}
}  // namespace

UTest cases{"raster.trim", [] {
    const auto line = segment({2, 8}, {122, 8});  // 120 long

    "a trimmed open line strokes the stretch between its fractions"_test = [&] {
        expect(same(trimmed(line, .25f, .75f), plain(segment({32, 8}, {92, 8}))));
        expect(same(trimmed(line, .75f, .25f), trimmed(line, .25f, .75f))) << "reversed fractions are swapped";
        expect(same(trimmed(line, 0, 1), plain(line))) << "the whole length is the plain stroke";
    };

    "an offset moves the stretch; past an open line's end it splits in two"_test = [&] {
        expect(same(trimmed(line, 0, .25f, .25f), plain(segment({32, 8}, {62, 8}))));
        raster::PathBuilder split;
        split.move_to({107, 8}).line_to({122, 8}).move_to({2, 8}).line_to({17, 8});
        expect(same(trimmed(line, 0, .25f, .875f), plain(split)));
        expect(same(trimmed(line, 0, .25f, -.125f), trimmed(line, 0, .25f, .875f))) << "offsets wrap by whole lengths";
    };

    "round a closed contour the stretch wraps through its start with its joins"_test = [] {
        raster::PathBuilder wrapped;
        wrapped.move_to({10, 30}).line_to({10, 10}).line_to({50, 10}).line_to({50, 30});
        expect(same(trimmed(square(), 0, .5f, .875f), plain(wrapped)));
        expect(same(trimmed(square(), 0, 1, .3f), plain(square()))) << "a whole turn, wherever it begins, draws the closed contour";
    };

    "an empty stretch draws nothing; nonfinite trims fall back to the whole"_test = [&] {
        expect(empty(trimmed(line, .5f, .5f)));
        const float nan = std::numeric_limits<float>::quiet_NaN();
        expect(same(trimmed(line, nan, nan, nan), plain(line)));
    };

    "trimming comes before dashing: the dashes run along the trimmed stretch"_test = [&] {
        const std::array pattern{10.0f, 5.0f};
        const auto dashed =
            stroked(line, {.width = 2, .dashes = pattern, .trim_start = .25f, .trim_end = .75f});
        expect(same(dashed, stroked(segment({32, 8}, {92, 8}), {.width = 2, .dashes = pattern})));
    };
}};
