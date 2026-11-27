// Selectors and deformers (convergence plan §6.8 ④): a part's geometry
// follows its value channels, so the crossbar of an "H" pulled down and let
// go on a spring overshoots past its rest and settles, while the legs stay
// put; scale and rotate deform about an anchor; an interruption goes on from
// the pulled geometry.
import std;
import hanim.engine;
import mcpplibs.mtest;

using namespace hanim;
using namespace mcpplibs::mtest;

namespace {
inline constexpr auto pull = ch::define<Vec2>("glyph.pull");
inline constexpr auto turn = ch::define<double>("glyph.turn");
inline constexpr auto stretch = ch::define<Vec2>("glyph.stretch", {1, 1});

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
// An "H" 100 × 100: legs 30 wide, a crossbar between y 45 and 55.
Contour letter_h() {
    const std::array<Vec2, 12> corners{{{30, 0},
                                        {30, 45},
                                        {70, 45},
                                        {70, 0},
                                        {100, 0},
                                        {100, 100},
                                        {70, 100},
                                        {70, 55},
                                        {30, 55},
                                        {30, 100},
                                        {0, 100},
                                        {0, 0}}};
    std::vector<ShapeSegment> edges;
    for (const auto corner : std::span(corners).subspan(1))
        edges.push_back(ShapeSegment::line(corner));
    return take(Contour::copy({corners[0], edges, true}));
}
// The crossbar's points have full weight; the legs' (more than 6 away) none.
const Selector crossbar = Selector::inside({35, 44}, {65, 56}, 6);

ObjectDescription glyph(std::vector<PartDeformer> deformers) {
    return {{{.id = "H",
              .contours = {letter_h()},
              .fill = Rgba{0, 0, 0, 1},
              .deformers = std::move(deformers)}}};
}
struct Scene {
    Engine engine = take(Engine::make());
    ObjectRef letter;
    explicit Scene(std::vector<PartDeformer> deformers)
        : letter(take(engine.create_object(glyph(std::move(deformers))))) {}
    FrameView now(PlaybackRef playback) {
        return take(engine.frame(playback)).geometry;
    }
};
// The crossbar's lower edge runs from (70, 55) to (30, 55), the eighth edge;
// its first control point, (56.7, 55), lies inside the selection.
double crossbar_y(const FrameView& frame) {
    return frame.cubics[7].control1.y;
}
// The legs' outer edges, x = 0 and x = 100, lie beyond the feather.
bool legs_still(const FrameView& frame) {
    const auto& right = frame.cubics[4];
    const auto& left = frame.cubics[10];
    return right.start == Vec2{100, 0} && right.end == Vec2{100, 100} &&
           left.start == Vec2{0, 100} && left.end == Vec2{0, 0} && right.control1.x == 100 &&
           left.control2.x == 0;
}
}  // namespace

UTest cases{"hanim.library.deform", [] {

    "a selection weighs its points: full inside, falling off to none"_test = [] {
        expect(crossbar.weight({50, 50}) == 1 && crossbar.weight({20, 50}) == 0 &&
              crossbar.weight({50, 20}) == 0);
        const double edge = crossbar.weight({68, 50});  // 3 of the 6 out
        expect(edge > 0 && edge < 1) << std::format("half way out of the feather: {}", edge);
        const auto round = Selector::around({0, 0}, 10, ease::linear);
        expect(round.weight({0, 0}) == 1 && round.weight({5, 0}) == .5 &&
              round.weight({10, 0}) == 0);
        expect(Selector::everything().weight({1e6, -1e6}) == 1);
    };

    "pulled down and let go, the crossbar overshoots its rest and settles; the legs stay"_test = [] {
                  Scene scene({deform::displace(crossbar, pull)});
                  const auto program =
                      sequence(anim::to(part(scene.letter, "H"), pull = Vec2{0, 20})
                                   .duration(seconds(0)),
                               anim::to(part(scene.letter, "H"), pull = Vec2{})
                                   .spring(springs::wobbly));
                  const auto playback = take(scene.engine.play(program));
                  expect(crossbar_y(scene.now(playback)) == 75) << "pulled 20 down from 55";
                  double highest = 75;
                  bool legs = true;
                  for (int step = 1; step <= 200; ++step) {
                      take(scene.engine.advance(seconds(1.0 / 120)));
                      const auto frame = scene.now(playback);
                      highest = std::min(highest, crossbar_y(frame));
                      legs = legs && legs_still(frame);
                  }
                  expect(highest < 54) << std::format("past its rest at 55, up to {}", highest);
                  expect(legs) << "the legs never moved";
                  take(scene.engine.advance(seconds(5)));
                  expect(std::abs(crossbar_y(scene.now(playback)) - 55) < 1e-6) << "settled at rest";
              };

    "an interruption goes on from the pulled geometry"_test = [] {
        Scene scene({deform::displace(crossbar, pull)});
        const auto playback = take(scene.engine.play(
            anim::to(part(scene.letter, "H"), pull = Vec2{0, 20}).duration(seconds(1))));
        take(scene.engine.advance(seconds(.5)));
        const double before = crossbar_y(scene.now(playback));
        expect(before > 55 && before < 75) << std::format("half pulled: {}", before);
        take(scene.engine.retarget(playback,
                                   PropertyTargets{.part = "H", .values = {pull.value({0, -10})}},
                                   {.duration = seconds(1)}));
        expect(std::abs(crossbar_y(scene.now(playback)) - before) < 1e-9) << "no jump at the interruption";
        take(scene.engine.advance(seconds(1)));
        expect(std::abs(crossbar_y(scene.now(playback)) - 45) < 1e-9) << "then to its new target, 10 up";
    };

    "scale and rotate deform about their anchor, in proportion to the weight"_test = [] {
        Scene scene({deform::rotate(crossbar, turn, {50, 50}),
                     deform::scale(Selector::everything(), stretch, {50, 100})});
        const auto quarter = take(scene.engine.play(
            anim::to(part(scene.letter, "H"), turn = std::numbers::pi / 2).duration(seconds(1))));
        take(scene.engine.advance(seconds(1)));
        // (56.7, 55) about (50, 50) a quarter turn on a y-down screen: (45, 56.7).
        const auto turned = scene.now(quarter).cubics[7].control1;
        expect(std::abs(turned.x - 45) < 1e-9 && std::abs(turned.y - (50 + 20.0 / 3)) < 1e-9) << std::format("the crossbar's point turned to ({}, {})", turned.x, turned.y);
        Scene tall({deform::scale(Selector::everything(), stretch, {50, 100})});
        const auto taller = take(tall.engine.play(
            anim::to(part(tall.letter, "H"), stretch = Vec2{1, 2}).duration(seconds(1))));
        take(tall.engine.advance(seconds(1)));
        expect(crossbar_y(tall.now(taller)) == 10) << "twice as tall from the baseline: 55 becomes 10";
    };

    "deformers name a part's own channels; malformed ones are refused"_test = [] {
        Engine engine = take(Engine::make());
        auto builtin = glyph({deform::displace(crossbar, ch::position)});
        const auto object = take(engine.create_object(builtin));
        expect(!engine.prepare(anim::to(part(object, "H"), pull = Vec2{1, 1}))) << "a built-in channel is no deformer's";
        auto shapeless = glyph({deform::displace(Selector::around({0, 0}, 0), pull)});
        expect(!engine.create_object(shapeless)) << "a selection without a radius";
        auto inverted = glyph({deform::displace(Selector::inside({10, 10}, {0, 0}, 1), pull)});
        expect(!engine.create_object(inverted)) << "a box inside out";
    };
}};
