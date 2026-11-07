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

    suite.test("a figure of several inks one soft body: it carries its other parts; it stands on a ledge, walks when pushed, hops", [] {
        // A pixel face (hanim.object's pixel picture): the outline ink's region is the silhouette; body and eye ride it.
        const PixelGrid face{{{'o', Rgba{.1f, .1f, .3f, 1}}, {'b', Rgba{.3f, .3f, .9f, 1}}, {'w', Rgba{1, 1, 1, 1}}},
                             {"oooo", "obbo", "obwo", "oooo"}, 10};
        Engine engine = take(Engine::make());
        auto description = take(pixel_object("face", face));
        description.parts[0].local = {.tx = 80, .ty = 40};
        const auto object = take(engine.create_object(description));
        auto showing = take(engine.play(anim::morph(object, object)));
        // The box of a part where it is drawn (its parent's space: the root's).
        const auto box_of = [&](std::string_view id) {
            const auto frame = take(engine.frame(showing)).geometry;
            Drawn out{1e9, -1e9, 1e9, -1e9, {}};
            for (const auto& part : frame.parts) {
                if (part.info.id != id) continue;
                for (std::size_t c = part.info.first_contour; c < part.info.first_contour + part.info.contour_count; ++c)
                    for (std::size_t k = frame.contours[c].first_cubic; k < frame.contours[c].first_cubic + frame.contours[c].cubic_count; ++k)
                        for (const auto point : {frame.cubics[k].start, frame.cubics[k].end}) {
                            out.left = std::min(out.left, point.x), out.right = std::max(out.right, point.x);
                            out.top = std::min(out.top, point.y), out.bottom = std::max(out.bottom, point.y);
                        }
            }
            return out;
        };
        check(!engine.simulate({object, ink_id('o')}, {.carries = {"nope"}}), "it carries only parts beside it");
        const auto body = take(engine.simulate({object, ink_id('o')}, {.params = {.shape_stiffness = 0.3}, .colliders = {ground}, .carries = {ink_id('b'), ink_id('w')}}));
        const std::array ledge{sim::Ledge{.left = -20, .right = 80, .top = 200}};
        take(engine.surfaces(body, ledge));
        for (int step = 0; step < 120 * 3; ++step) take(engine.advance(seconds(1.0 / 120)));
        const auto silhouette = box_of(ink_id('o')), eye = box_of(ink_id('w'));
        check(std::abs(silhouette.bottom - 200) < 1, std::format("on the ledge, not the ground: {}", silhouette.bottom));
        check(eye.top > silhouette.top && eye.bottom < silhouette.bottom && eye.left > silhouette.left && eye.right < silhouette.right,
              "the eye rode the body down, inside it");
        // Another expression on the same body: the eye moves to the left, the body stays where it stands.
        PixelGrid looking = face;
        looking.rows = {"oooo", "obbo", "owbo", "oooo"};
        auto changed = take(pixel_object("face", looking));
        changed.parts[0].local = {.tx = 80, .ty = 40};
        take(engine.update_object(object, changed));
        take(engine.release(showing));
        showing = take(engine.play(anim::morph(object, object)));
        const auto again = showing;
        const std::array carried{ink_id('b'), ink_id('w')};
        take(engine.carry(body, carried));
        const auto box_again = [&](std::string_view id) {
            const auto frame = take(engine.frame(again)).geometry;
            double left = 1e9, bottom = -1e9;
            for (const auto& part : frame.parts) {
                if (part.info.id != id) continue;
                for (std::size_t c = part.info.first_contour; c < part.info.first_contour + part.info.contour_count; ++c)
                    for (std::size_t k = frame.contours[c].first_cubic; k < frame.contours[c].first_cubic + frame.contours[c].cubic_count; ++k)
                        left = std::min(left, frame.cubics[k].start.x), bottom = std::max(bottom, frame.cubics[k].start.y);
            }
            return std::pair{left, bottom};
        };
        check(std::abs(box_again(ink_id('o')).second - 200) < 1, std::format("the new figure where the body stands: {}", box_again(ink_id('o')).second));
        check(box_again(ink_id('w')).first < eye.left - 5, std::format("the eye moved left: {} → {}", eye.left, box_again(ink_id('w')).first));
        check(!engine.carry(body, std::array{std::string("nope")}), "only parts beside it");
        // Walking: off the ledge's end it drops to the ground (a push alone does not slide it: its feet hold).
        take(engine.walk(body, 200));
        for (int step = 0; step < 120 * 3; ++step) take(engine.advance(seconds(1.0 / 120)));
        take(engine.walk(body, 0));
        for (int step = 0; step < 120 * 2; ++step) take(engine.advance(seconds(1.0 / 120)));
        check(std::abs(box_of(ink_id('o')).bottom - 300) < 1, std::format("walked off, on the ground: {}", box_of(ink_id('o')).bottom));
        // A hop: up, then down again.
        const double before = box_of(ink_id('o')).top;
        take(engine.hop(body, {0, -500}));
        double highest = before;
        for (int step = 0; step < 120; ++step) {
            take(engine.advance(seconds(1.0 / 120)));
            highest = std::min(highest, box_of(ink_id('o')).top);
        }
        check(highest < before - 40, std::format("it hopped: {} → {}", before, highest));
        std::vector<sim::Ledge> too_many(sim::max_ledges + 1, sim::Ledge{0, 1, 0});
        check(!engine.surfaces(body, too_many), "more ledges than a step holds: refused");
    });

    return suite.run(argc, argv);
}
