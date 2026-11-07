// Simulated bodies (convergence plan §6.8 ⑨ ①②): a part made soft by
// xengine.simulation's solver, its whole geometry carried by a cage over the
// body's ring — a ball and a letter with a hole fall, flatten on the ground,
// bounce and settle, never through it; a hand drags one; the same inputs give
// the same frames.
import std;
import hanim.engine;
import mlibs.testing;

using namespace hanim;
using namespace mlibs::testing;
namespace sim = xengine::simulation;

namespace {
template <class T, class E>
T take(std::expected<T, E> result) {
    if constexpr (requires { result.error().text(); })
        check(bool(result), result ? std::string() : result.error().text());
    else
        check(bool(result));
    if (!result)
        std::abort();
    if constexpr (!std::is_void_v<T>)
        return std::move(*result);
}
Contour circle(double radius) {
    const double k = radius * .5522847498307936;
    const std::array arcs{ShapeSegment::cubic({k, -radius}, {radius, -k}, {radius, 0}),
                          ShapeSegment::cubic({radius, k}, {k, radius}, {0, radius}),
                          ShapeSegment::cubic({-k, radius}, {-radius, k}, {-radius, 0}),
                          ShapeSegment::cubic({-radius, -k}, {-k, -radius}, {0, -radius})};
    return take(Contour::copy({{0, -radius}, arcs, true}));
}
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
const sim::Plane ground{.normal_x = 0, .normal_y = -1, .offset = -300};
struct Drawn {
    double left, right, top, bottom;
    std::vector<ShapeCubic> cubics;
};
// The part's geometry where it is drawn (in its parent's space).
Drawn drawn(Engine& engine, PlaybackRef playback) {
    const auto frame = take(engine.frame(playback)).geometry;
    const auto& local = frame.parts[0].local;
    const auto matrix = local.matrix();
    Drawn result{1e9, -1e9, 1e9, -1e9, {frame.cubics.begin(), frame.cubics.end()}};
    for (const auto& cubic : frame.cubics)
        for (const auto point : {cubic.start, cubic.end}) {
            const double x = matrix[0] * point.x + matrix[2] * point.y + matrix[4],
                         y = matrix[1] * point.x + matrix[3] * point.y + matrix[5];
            result.left = std::min(result.left, x), result.right = std::max(result.right, x);
            result.top = std::min(result.top, y), result.bottom = std::max(result.bottom, y);
        }
    return result;
}
struct Dropped {
    double flattest = 1, deepest = 0;
    Drawn rest;
};
Dropped drop(Engine& engine, ObjectRef object, std::string part) {
    const auto shown = take(engine.play(anim::morph(object, object)));
    take(engine.simulate({object, part}, {.colliders = {ground}}));
    Dropped result;
    for (int step = 0; step < 120 * 5; ++step) {
        take(engine.advance(seconds(1.0 / 120)));
        const auto now = drawn(engine, shown);
        result.flattest =
            std::min(result.flattest, (now.bottom - now.top) / (now.right - now.left));
        result.deepest = std::max(result.deepest, now.bottom - 300);
    }
    result.rest = drawn(engine, shown);
    return result;
}
}  // namespace

int main(int argc, char** argv) {
    Suite suite{"hanim.engine.body"};

    suite.test("made soft, a ball keeps its shape until it moves; dropped, it flattens, bounces "
               "and rests on the ground",
               [] {
                   Engine engine = take(Engine::make());
                   const auto ball = take(engine.create_object({{{.id = "ball",
                                                                  .local = {.tx = 100, .ty = 100},
                                                                  .contours = {circle(20)},
                                                                  .fill = Rgba{1, 0, 0, 1}}}}));
                   const auto shown = take(engine.play(anim::morph(ball, ball)));
                   const auto before = drawn(engine, shown);
                   const auto body = take(engine.simulate({ball, "ball"}, {.colliders = {ground}}));
                   const auto made = drawn(engine, shown);
                   double moved = 0;
                   for (std::size_t index = 0; index < made.cubics.size(); ++index)
                       moved = std::max(
                           {moved,
                            std::abs(made.cubics[index].start.x - before.cubics[index].start.x),
                            std::abs(made.cubics[index].control1.y -
                                     before.cubics[index].control1.y)});
                   check(moved < 1e-9,
                         std::format("the cage reproduces the shape it was made on: {}", moved));
                   double flattest = 1, deepest = 0;
                   for (int step = 0; step < 120 * 5; ++step) {
                       take(engine.advance(seconds(1.0 / 120)));
                       const auto now = drawn(engine, shown);
                       flattest =
                           std::min(flattest, (now.bottom - now.top) / (now.right - now.left));
                       deepest = std::max(deepest, now.bottom - 300);
                   }
                   check(flattest < .95, std::format("flattened on landing: {}", flattest));
                   check(deepest < 1, std::format("never through the ground beyond {}", deepest));
                   const auto rest = drawn(engine, shown);
                   check(std::abs(rest.bottom - 300) < 1.5 &&
                             std::abs((rest.right - rest.left) - 40) < 4,
                         std::format("at rest on the ground, {} wide", rest.right - rest.left));
                   check(take(engine.body(body))->size() == 48);
               });

    suite.test("any outline: a letter with a hole falls and settles the same way", [] {
        Engine engine = take(Engine::make());
        const auto letter = take(engine.create_object(
            {{{.id = "O",
               .local = {.tx = 80, .ty = 60},
               .contours = {rectangle(0, 0, 40, 50), rectangle(12, 12, 16, 26, true)},
               .fill = Rgba{0, 0, 0, 1}}}}));
        const auto dropped = drop(engine, letter, "O");
        check(dropped.flattest < .95 * 50.0 / 40.0,
              std::format("it flattens: {}", dropped.flattest));
        check(dropped.deepest < 1 && std::abs(dropped.rest.bottom - 300) < 1.5,
              std::format("rests on the ground at {}", dropped.rest.bottom));
    });

    suite.test("a hand drags the body and lets it go; the same inputs give the same frames", [] {
        const auto run = [] {
            Engine engine = take(Engine::make());
            const auto ball = take(engine.create_object({{{.id = "ball",
                                                           .local = {.tx = 100, .ty = 100},
                                                           .contours = {circle(20)},
                                                           .fill = Rgba{1, 0, 0, 1}}}}));
            const auto shown = take(engine.play(anim::morph(ball, ball)));
            const auto body = take(engine.simulate({ball, "ball"}, {.params = {.gravity_y = 0}}));
            for (int step = 0; step < 60; ++step) {
                take(engine.pull(body, Vec2{120 + step * 1.0, 100}));
                take(engine.advance(seconds(1.0 / 120)));
            }
            const double held = take(engine.body(body))->centre()[0];
            take(engine.pull(body, std::nullopt));
            take(engine.advance(seconds(.5)));
            return std::pair{held, drawn(engine, shown).cubics};
        };
        const auto [held, frames] = run();
        check(held > 110, std::format("dragged along: centre at {}", held));
        const auto [again, same] = run();
        const auto bits = [](const ShapeCubic& cubic) {
            return std::bit_cast<std::array<std::uint64_t, 8>>(cubic);
        };
        check(again == held && std::ranges::equal(frames, same,
                                                  [&](const auto& a, const auto& b) {
                                                      return bits(a) == bits(b);
                                                  }),
              "deterministic");
    });

    suite.test("a body needs a part a playback shows, once; its handle goes stale when removed",
               [] {
                   Engine engine = take(Engine::make());
                   const auto ball = take(engine.create_object(
                       {{{.id = "ball", .contours = {circle(10)}, .fill = Rgba{1, 0, 0, 1}}}}));
                   check(!engine.simulate({ball, "ball"}), "nothing draws it yet");
                   const auto shown = take(engine.play(anim::morph(ball, ball)));
                   const auto body = take(engine.simulate({ball, "ball"}));
                   check(!engine.simulate({ball, "ball"}), "once");
                   take(engine.unsimulate(body));
                   check(!engine.body(body) && !engine.pull(body, Vec2{0, 0}), "stale");
                   (void)shown;
               });

    return suite.run(argc, argv);
}
