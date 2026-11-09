// Topology-aligning morphs (convergence plan §6.8 ③): parts whose ends have
// different contours morph continuously — outlines pair with outlines and
// holes with holes, and a contour left over grows from a point or closes
// into one — sampled over the whole course, continued from a frame.
import std;
import hanim.library;
import mlibs.geom;
import mcpplibs.mtest;

using namespace hanim;
using namespace mcpplibs::mtest;

namespace {
template <class T, class E>
T take(std::expected<T, E> result) {
    if constexpr (requires { result.error().text(); })
        expect(bool(result)) << (result ? std::string() : result.error().text());
    else
        expect(bool(result));
    if (!result)
        std::abort();
    return std::move(*result);
}
// A rectangle, clockwise on a y-down screen (outline) or the other way (hole).
Contour rectangle(double left, double top, double width, double height, bool hole = false) {
    const double right = left + width, bottom = top + height;
    const std::array outline{ShapeSegment::line({right, top}), ShapeSegment::line({right, bottom}),
                             ShapeSegment::line({left, bottom})};
    const std::array inverse{ShapeSegment::line({left, bottom}),
                             ShapeSegment::line({right, bottom}), ShapeSegment::line({right, top})};
    return take(Contour::copy(
        {{left, top},
         hole ? std::span<const ShapeSegment>(inverse) : std::span<const ShapeSegment>(outline),
         true}));
}
ObjectDescription glyph(std::vector<Contour> contours) {
    return {{{.id = "glyph", .contours = std::move(contours), .fill = Rgba{0, 0, 0, 1}}}};
}
struct Course {
    ObjectRegistry registry = take(ObjectRegistry::make());
    PreparedObjectAnimation plan;
    std::vector<FramePart> parts;
    std::vector<ShapeCubic> cubics;
    Course(const ObjectDescription& from, const ObjectDescription& to, MorphOptions options = {}) {
        const auto source = take(registry.capture(take(registry.create(from))));
        const auto target = take(registry.capture(take(registry.create(to))));
        plan = take(PreparedObjectAnimation::prepare(source, target, options));
        parts.resize(plan.requirements().parts);
        cubics.resize(plan.requirements().cubics);
    }
    FrameView at(double progress) {
        return take(plan.sample(progress, {parts, cubics}));
    }
};
mlibs::geom::ContourMeasure measured(const FrameView& frame, std::size_t contour) {
    const auto& range = frame.contours[contour];
    return mlibs::geom::measure(frame.cubics.subspan(range.first_cubic, range.cubic_count));
}
// The filled area: outlines add, holes (the other winding) take away.
double filled(const FrameView& frame) {
    double total = 0;
    for (std::size_t contour = 0; contour < frame.contours.size(); ++contour)
        total += measured(frame, contour).area;
    return std::abs(total);
}
bool is_point(const FrameView& frame, std::size_t contour, Vec2 point) {
    const auto& range = frame.contours[contour];
    return std::ranges::all_of(frame.cubics.subspan(range.first_cubic, range.cubic_count),
                               [&](const ShapeCubic& cubic) {
                                   return cubic.start == point && cubic.control1 == point &&
                                          cubic.control2 == point && cubic.end == point;
                               });
}
bool near(double left, double right, double tolerance = 1e-9) {
    return std::abs(left - right) <= tolerance;
}
// Sampled finely, the filled area never jumps: each step's change is bounded.
void check_continuous(Course& course, double largest_step) {
    double previous = filled(course.at(0));
    for (int step = 1; step <= 100; ++step) {
        const double now = filled(course.at(step / 100.0));
        expect(std::abs(now - previous) <= largest_step) << std::format("the area jumps from {} to {} at {}", previous, now, step / 100.0);
        previous = now;
    }
}
}  // namespace

UTest cases{"hanim.library.topology", [] {

    "\"一\" splits into \"二\": the second bar grows out of the first"_test = [] {
        // 一: one bar across the middle; 二: a short bar above, a long one below.
        Course course(glyph({rectangle(10, 45, 80, 10)}),
                      glyph({rectangle(20, 20, 60, 10), rectangle(10, 70, 80, 10)}));
        const auto start = course.at(0);
        expect(start.contours.size() == 2 && start.parts[0].info.contour_count == 2) << "a pair and a growing bar";
        expect(near(measured(start, 0).area, 800) && is_point(start, 1, {50, 50})) << "一 itself, the new bar a point at its centre";
        const auto middle = course.at(.5);
        const auto grown = measured(middle, 1);
        expect(grown.area > 100 && grown.area < 700) << std::format("halfway the new bar is part grown: {}", grown.area);
        const auto end = course.at(1);
        // The long bar pairs with 一 (the same size, nearest); the short one grows.
        expect(near(measured(end, 0).area, 800) && near(measured(end, 0).centroid_y, 75)) << "一 became the lower bar";
        expect(near(measured(end, 1).area, 600) && near(measured(end, 1).centroid_y, 25)) << "the upper bar has grown in full";
        check_continuous(course, 60);
    };

    "holes pair with holes; a hole left over opens from its own centre or closes into it"_test = [] {
                  // An outline with one hole to the same outline with two (A towards B).
                  const auto outer = rectangle(0, 0, 100, 100);
                  Course opening(glyph({outer, rectangle(20, 10, 60, 30, true)}),
                                 glyph({outer, rectangle(20, 10, 60, 30, true),
                                        rectangle(20, 60, 60, 30, true)}));
                  const auto start = opening.at(0);
                  expect(start.contours.size() == 3 && is_point(start, 2, {50, 75})) << "the second hole a point at its own centre";
                  expect(near(filled(start), 10000 - 1800)) << "the start is the first glyph";
                  expect(near(filled(opening.at(1)), 10000 - 3600)) << "the end is the second";
                  expect(measured(opening.at(.5), 2).area < 0) << "the opening hole runs the holes' way round";
                  check_continuous(opening, 60);
                  // And back: the hole closes into its centre.
                  Course closing(glyph({outer, rectangle(20, 10, 60, 30, true),
                                        rectangle(20, 60, 60, 30, true)}),
                                 glyph({outer, rectangle(20, 10, 60, 30, true)}));
                  const auto end = closing.at(1);
                  expect(end.contours.size() == 3 && is_point(end, 2, {50, 75}) &&
                        near(filled(end), 10000 - 1800));
              };

    "an outline left over merges into the nearest outline; strict pairing refuses"_test = [] {
        const ObjectDescription two = glyph({rectangle(20, 20, 60, 10), rectangle(10, 70, 80, 10)});
        const ObjectDescription one = glyph({rectangle(10, 45, 80, 10)});
        Course merging(two, one);
        const auto end = merging.at(1);
        expect(end.contours.size() == 2 && is_point(end, 1, {50, 50}) && near(filled(end), 800)) << "the short bar closes into 一's centre";
        ObjectRegistry registry = take(ObjectRegistry::make());
        const auto source = take(registry.capture(take(registry.create(two))));
        const auto target = take(registry.capture(take(registry.create(one))));
        const auto strict =
            PreparedObjectAnimation::prepare(source, target, {.contours = ContourPairing::strict});
        expect(!strict && strict.error().code == MorphError::contour_count_mismatch);
    };

    "a morph continued from a grown frame ignores the point it grew from"_test = [] {
        Course course(glyph({rectangle(10, 45, 80, 10)}),
                      glyph({rectangle(20, 20, 60, 10), rectangle(10, 70, 80, 10)}));
        const auto start = course.at(0);  // 一 and a point
        const auto back = take(course.registry.capture(
            take(course.registry.create(glyph({rectangle(10, 45, 80, 10)})))));
        const auto again = take(PreparedObjectAnimation::retarget(start, back));
        std::vector<FramePart> parts(again.requirements().parts);
        std::vector<ShapeCubic> cubics(again.requirements().cubics);
        const auto frame = take(again.sample(.5, {parts, cubics}));
        expect(frame.contours.size() == 1 && near(filled(frame), 800)) << "一 to 一: the point was no contour";
    };
}};
