// Match modifiers and path noise (convergence plan §6.8 ⑤): the same A→B
// under other matching rules stays a valid, continuous morph; noise along
// the way keeps both ends and the contour's seams exact and is a pure
// function of time, whatever order it is sampled in.
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
Contour rectangle(double left, double top, double width, double height) {
    const double right = left + width, bottom = top + height;
    const std::array edges{ShapeSegment::line({right, top}), ShapeSegment::line({right, bottom}),
                           ShapeSegment::line({left, bottom})};
    return take(Contour::copy({{left, top}, edges, true}));
}
Contour circle(Vec2 centre, double radius) {
    constexpr double k = .5522847498307936;
    const auto at = [&](double x, double y) {
        return Vec2{centre.x + radius * x, centre.y + radius * y};
    };
    const std::array arcs{ShapeSegment::cubic(at(k, -1), at(1, -k), at(1, 0)),
                          ShapeSegment::cubic(at(1, k), at(k, 1), at(0, 1)),
                          ShapeSegment::cubic(at(-k, 1), at(-1, k), at(-1, 0)),
                          ShapeSegment::cubic(at(-1, -k), at(-k, -1), at(0, -1))};
    return take(Contour::copy({at(0, -1), arcs, true}));
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
    std::vector<ShapeCubic> at(double progress) {
        const auto frame = take(plan.sample(progress, {parts, cubics}));
        return {frame.cubics.begin(), frame.cubics.end()};
    }
};
bool same_bits(const std::vector<ShapeCubic>& left, const std::vector<ShapeCubic>& right) {
    const auto bits = [](Vec2 point) {
        return std::pair{std::bit_cast<std::uint64_t>(point.x),
                         std::bit_cast<std::uint64_t>(point.y)};
    };
    return left.size() == right.size() &&
           std::ranges::equal(left, right, [&](const ShapeCubic& a, const ShapeCubic& b) {
               return bits(a.start) == bits(b.start) && bits(a.control1) == bits(b.control1) &&
                      bits(a.control2) == bits(b.control2) && bits(a.end) == bits(b.end);
           });
}
double largest_gap(const std::vector<ShapeCubic>& left, const std::vector<ShapeCubic>& right) {
    double gap = 0;
    for (std::size_t index = 0; index < left.size(); ++index)
        for (const auto [a, b] : {std::pair{left[index].start, right[index].start},
                                  std::pair{left[index].control1, right[index].control1}})
            gap = std::max(gap, std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)));
    return gap;
}
}  // namespace

UTest cases{"hanim.library.process", [] {
    const auto square = glyph({rectangle(0, 0, 100, 100)});
    const auto round = glyph({circle({50, 50}, 50)});

    "noise along the way keeps both ends exact and moves the middle within its amplitude"_test = [&] {
                  for (const auto noise :
                       {PointPath::jitter(6, 25, 3), PointPath::swirl(6, 25, 3)}) {
                      Course plain(square, round),
                          noisy(square, round, {.shape = {.point_path = noise}});
                      expect(same_bits(plain.at(0), noisy.at(0)) &&
                                same_bits(plain.at(1), noisy.at(1))) << "the ends are the plain morph's, bit for bit";
                      const double middle = largest_gap(plain.at(.5), noisy.at(.5));
                      expect(middle > .5 && middle <= 6 * std::sqrt(2.0) + 1e-9) << std::format("halfway the noise moves points by up to {}", middle);
                      expect(largest_gap(plain.at(.01), noisy.at(.01)) < .02) << "the envelope starts it gently";
                      const auto frame = noisy.at(.37);
                      for (std::size_t index = 0; index < frame.size(); ++index)
                          expect(frame[index].end == frame[(index + 1) % frame.size()].start) << "the seams stay shared";
                  }
              };

    "noise is a function of time and seed: any order of seeks, the same frames"_test = [&] {
        Course noisy(square, round, {.shape = {.point_path = PointPath::jitter(6, 25, 3)}});
        const auto forwards = noisy.at(.3), later = noisy.at(.8), again = noisy.at(.3);
        expect(same_bits(forwards, again) && !same_bits(forwards, later));
        Course other(square, round, {.shape = {.point_path = PointPath::jitter(6, 25, 4)}});
        expect(!same_bits(other.at(.3), forwards)) << "another seed, another way";
    };

    mcpplibs::mtest::test(std::string_view("other matching rules give another valid course; pins pair first; bad rules are "
               "refused")) = [&] {
                   // 一 into 二: by default 一 becomes the long lower bar.
                   const auto one = glyph({rectangle(10, 45, 80, 10)});
                   const auto two = glyph({rectangle(20, 20, 60, 10), rectangle(10, 70, 80, 10)});
                   const auto lower_y = [](const std::vector<ShapeCubic>& frame) {
                       return mlibs::geom::measure(std::span<const ShapeCubic>(frame).first(4))
                           .centroid_y;
                   };
                   Course fitting(one, two);
                   expect(std::abs(lower_y(fitting.at(1)) - 75) < 1e-9) << "by size and place, the lower bar";
                   Course pinned(one, two, {.match = {.pins = {{"glyph", 0, 0}}}});
                   expect(std::abs(lower_y(pinned.at(1)) - 25) < 1e-9) << "pinned to the upper bar";
                   expect(pinned.plan.contours().size() == 2 && fitting.plan.contours().size() == 2) << "the other bar still grows";
                   ObjectRegistry registry = take(ObjectRegistry::make());
                   const auto source = take(registry.capture(take(registry.create(one))));
                   const auto target = take(registry.capture(take(registry.create(two))));
                   const auto twice =
                       PreparedObjectAnimation::prepare(source, target,
                                                        {.match = {.pins = {{"glyph", 0, 0},
                                                                            {"glyph", 0, 1}}}});
                   expect(!twice && twice.error().code == MorphError::invalid_options) << "one contour pinned twice";
                   const auto negative =
                       PreparedObjectAnimation::prepare(source, target,
                                                        {.match = {.distance = -1}});
                   expect(!negative && negative.error().code == MorphError::invalid_options) << "a negative weight";
                   const auto wild =
                       PreparedObjectAnimation::prepare(source, target,
                                                        {.shape = {.point_path =
                                                                       PointPath::jitter(1, 0)}});
                   expect(!wild) << "noise without a feature size";
               };
}};
