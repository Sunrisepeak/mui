// A drop (convergence plan §6.8 ⑨): any part falls under gravity, lands,
// bounces less each time and settles, squashing about its contact point as
// it lands and stretching as it speeds down — sampled at 240 Hz over the
// whole course; the curve, the channel and the deformer are general, not the
// ball's.
import std;
import hanim.engine;
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
    if constexpr (!std::is_void_v<T>)
        return std::move(*result);
}
// A ball of radius 20 about its origin; its contact point is its bottom.
ObjectDescription ball() {
    constexpr double k = 20 * .5522847498307936;
    const std::array arcs{ShapeSegment::cubic({k, -20}, {20, -k}, {20, 0}),
                          ShapeSegment::cubic({20, k}, {k, 20}, {0, 20}),
                          ShapeSegment::cubic({-k, 20}, {-20, k}, {-20, 0}),
                          ShapeSegment::cubic({-20, -k}, {-k, -20}, {0, -20})};
    return {{{.id = "ball",
              .contours = {take(Contour::copy({{0, -20}, arcs, true}))},
              .fill = Rgba{1, .3f, .2f, 1},
              .deformers = {deform::squash({0, 20})}}}};
}
struct Shape {
    double bottom = 0, width = 0, height = 0, position = 0;
};
Shape measure(const FrameView& frame) {
    double left = 1e9, right = -1e9, top = 1e9, bottom = -1e9;
    for (const auto& cubic : frame.cubics)
        for (const auto point : {cubic.start, cubic.end}) {
            left = std::min(left, point.x), right = std::max(right, point.x);
            top = std::min(top, point.y), bottom = std::max(bottom, point.y);
        }
    const double position = frame.parts[0].local.ty;
    return {position + bottom, right - left, bottom - top, position};
}
}  // namespace

UTest cases{"hanim.drop", [] {

    mcpplibs::mtest::test(std::string_view("a dropped ball lands, squashes about its bottom, bounces lower each time and "
               "settles")) = [] {
                   Engine engine = take(Engine::make());
                   const auto object = take(engine.create_object(ball()));
                   const anim::DropOptions options{.restitution = .45,
                                                   .bounces = 2,
                                                   .duration = seconds(1)};
                   const auto playback =
                       take(engine.play(anim::drop(part(object, "ball"), {0, 200}, options)));
                   // In first falls: 1 + .1 + 2(.45) + .1(.45) + 2(.45²) = 2.45; landings at 1, 2
                   // and 2.45 falls, on the ground .1 and .045 after the first two.
                   const double first_fall = 1 / 2.45;
                   double deepest = 0, widest_first = 0, widest_second = 0, tallest = 0;
                   for (int step = 1; step <= 360; ++step) {
                       take(engine.advance(seconds(1.0 / 240)));
                       const auto shape = measure(take(engine.frame(playback)).geometry);
                       const double time = step / 240.0;
                       deepest = std::max(deepest, shape.bottom);
                       if (time < first_fall)
                           tallest = std::max(tallest, shape.height);
                       if (time > first_fall && time < 2 * first_fall)
                           widest_first = std::max(widest_first, shape.width);
                       if (time > 2 * first_fall && time < 2.3 * first_fall)
                           widest_second = std::max(widest_second, shape.width);
                   }
                   expect(deepest <= 220 + 1e-9) << std::format("never below the ground: {}", deepest);
                   expect(tallest > 42) << std::format("stretched as it speeds down: {} tall", tallest);
                   expect(widest_first > 46 && widest_second > 40 && widest_second < widest_first) << std::format("squashed {} wide at the first landing, {} at the second",
                                     widest_first, widest_second);
                   // The last squash rounds quickly: the whole drop is not much longer than its fall.
                   const auto length = take(engine.inspect(playback)).duration.count();
                   expect(length > 1 && length < 1.5) << std::format("the drop lasts {} s for a 1 s fall", length);
                   take(engine.advance(seconds(5)));
                   const auto rest = measure(take(engine.frame(playback)).geometry);
                   expect(std::abs(rest.position - 200) < 1e-9 && std::abs(rest.width - 40) < 1e-6 &&
                             std::abs(rest.height - 40) < 1e-6) << std::format("at rest on the ground, round again: ({}, {} × {})",
                                     rest.position, rest.width, rest.height);
               };

    "the curve: on the ground at each landing, exactly, and the apexes lower each bounce"_test = [] {
                  const auto curve = Ease::fall(.45, 2);
                  expect(curve.valid() && std::abs(curve.falls() - 2.45) < 1e-12);
                  for (const double on_ground : {1.0, 1.05, 1.0999, 2.0, 2.02})
                      expect(std::abs(curve(on_ground / 2.45) - 1) < 1e-12) << std::format("on the ground at {} falls", on_ground);
                  expect(curve(1) == 1) << "and for good at the end";
                  expect(std::abs(curve(1.55 / 2.45) - (1 - .45 * .45)) < 1e-12) << "the first apex at .45² of the height";
                  expect(!Ease::fall(1, 2).valid() && !Ease::fall(.5, -1).valid()) << "no perpetual or negative bouncing";
                  expect(to_string(curve) == "fall(0.45, 2, 0.1)" &&
                            parse_ease("fall(0.45, 2, 0.1)") == curve &&
                            parse_ease("fall(0.45, 2)") == curve) << "named like the others";
              };

    "any part drops the same way: a bar of text squashes about its baseline"_test = [] {
        Engine engine = take(Engine::make());
        const std::array edges{ShapeSegment::line({80, 0}), ShapeSegment::line({80, 10}),
                               ShapeSegment::line({0, 10})};
        const auto bar =
            take(engine.create_object({{{.id = "bar",
                                         .contours = {take(Contour::copy({{0, 0}, edges, true}))},
                                         .fill = Rgba{0, 0, 0, 1},
                                         .deformers = {deform::squash({40, 10})}}}}));
        const auto playback = take(engine.play(anim::drop(part(bar, "bar"), {0, 100})));
        take(engine.advance(seconds(1.05 / 2.45)));  // halfway through its first landing
        const auto shape = measure(take(engine.frame(playback)).geometry);
        expect(shape.width > 80 && std::abs(shape.bottom - 110) < 1e-6) << std::format("{} wide, its baseline at {}", shape.width, shape.bottom);
    };
}};
