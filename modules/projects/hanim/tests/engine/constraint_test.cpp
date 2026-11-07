// Constraints and drivers (convergence plan §6.8 ⑦): one part made from
// another after every playback is sampled — a tag following a card through
// its parent's transform, an arrow aiming at it, a channel driven from
// another — in dependency order whatever order they were added in; cycles
// and rival constraints are refused.
import std;
import hanim.engine;
import mlibs.testing;

using namespace hanim;
using namespace mlibs::testing;

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
Contour square() {
    const std::array edges{ShapeSegment::line({10, 0}), ShapeSegment::line({10, 10}),
                           ShapeSegment::line({0, 10})};
    return take(Contour::copy({{0, 0}, edges, true}));
}
ObjectDescription single(std::string id, Transform2D local = {}) {
    return {
        {{.id = std::move(id), .local = local, .contours = {square()}, .fill = Rgba{1, 1, 1, 1}}}};
}
struct Scene {
    Engine engine = take(Engine::make());
    ObjectRef card = take(engine.create_object(single("card")));
    // The tag sits inside a group moved to (100, 0) and scaled twice.
    ObjectRef tag = take(engine.create_object(
        {{{.id = "group", .local = {.tx = 100, .sx = 2, .sy = 2}},
          {.id = "tag", .parent = "group", .contours = {square()}, .fill = Rgba{1, 1, 1, 1}}}}));
    ObjectRef arrow = take(engine.create_object(single("arrow", {.tx = 0, .ty = 100})));
    PlaybackRef moving = take(engine.play(
        anim::to(card, PropertyTargets{.position = Vec2{200, 50}}).duration(seconds(1))));
    PlaybackRef held = take(engine.play(anim::morph(tag, tag)));
    PlaybackRef pointing = take(engine.play(anim::morph(arrow, arrow)));
    Transform2D local(PlaybackRef playback, std::string_view part) {
        const auto frame = take(engine.frame(playback)).geometry;
        for (const auto& shown : frame.parts)
            if (shown.info.id == part)
                return shown.local;
        check(false, "the part shows");
        return {};
    }
};
bool near(double left, double right) {
    return std::abs(left - right) < 1e-9;
}
}  // namespace

int main(int argc, char** argv) {
    Suite suite{"hanim.engine.constraint"};

    suite.test("a tag follows a moving card in world space, through its parent's transform", [] {
        Scene scene;
        take(scene.engine.constrain({.kind = ConstraintKind::follow,
                                     .target = {scene.tag, "tag"},
                                     .source = {scene.card, "card"},
                                     .offset = {0, -12}}));
        for (const double time : {.25, .5, 1.0}) {
            take(scene.engine.advance(seconds(time == .25 ? .25 : .25 * (time == .5 ? 1 : 2))));
            const auto card = scene.local(scene.moving, "card"),
                       tag = scene.local(scene.held, "tag");
            // World (card.tx, card.ty − 12) in a group at (100, 0) scaled 2.
            check(near(tag.tx, (card.tx - 100) / 2) && near(tag.ty, (card.ty - 12) / 2),
                  std::format("at {}s the tag is at ({}, {}) for a card at ({}, {})", time, tag.tx,
                              tag.ty, card.tx, card.ty));
        }
    });

    suite.test("an arrow aims at the card; a channel is driven from another", [] {
        Scene scene;
        take(scene.engine.constrain({.kind = ConstraintKind::aim,
                                     .target = {scene.arrow, "arrow"},
                                     .source = {scene.card, "card"}}));
        take(scene.engine.constrain({.kind = ConstraintKind::relate,
                                     .target = {scene.arrow, "arrow"},
                                     .source = {scene.card, "card"},
                                     .offset = {1, 1},
                                     .scale = .01,
                                     .target_channel = "scale",
                                     .source_channel = "position"}));
        take(scene.engine.advance(seconds(1)));
        const auto arrow = scene.local(scene.pointing, "arrow");
        // From (0, 100) to the card at (200, 50).
        check(near(arrow.rotate, std::atan2(-50.0, 200.0)), std::format("turned {}", arrow.rotate));
        check(near(arrow.sx, 3) && near(arrow.sy, 1.5), "scale = 0.01 × position + 1");
    });

    suite.test("chains apply in dependency order, whatever order they were added in", [] {
        Scene scene;
        // The arrow follows the tag, which follows the card: added last first.
        take(scene.engine.constrain({.kind = ConstraintKind::follow,
                                     .target = {scene.arrow, "arrow"},
                                     .source = {scene.tag, "tag"}}));
        take(scene.engine.constrain({.kind = ConstraintKind::follow,
                                     .target = {scene.tag, "tag"},
                                     .source = {scene.card, "card"}}));
        take(scene.engine.advance(seconds(.5)));
        const auto card = scene.local(scene.moving, "card"),
                   arrow = scene.local(scene.pointing, "arrow");
        check(near(arrow.tx, card.tx) && near(arrow.ty, card.ty),
              "the arrow is where the card is, the same frame");
    });

    suite.test("a cycle and a rival are refused; a removed constraint frees its channel", [] {
        Scene scene;
        const auto first = take(scene.engine.constrain({.kind = ConstraintKind::follow,
                                                        .target = {scene.tag, "tag"},
                                                        .source = {scene.card, "card"}}));
        const auto cycle = scene.engine.constrain({.kind = ConstraintKind::follow,
                                                   .target = {scene.card, "card"},
                                                   .source = {scene.tag, "tag"}});
        check(!cycle && cycle.error().code == EngineError::constraint_cycle,
              "the card cannot follow its follower");
        const auto rival = scene.engine.constrain({.kind = ConstraintKind::follow,
                                                   .target = {scene.tag, "tag"},
                                                   .source = {scene.arrow, "arrow"}});
        check(!rival && rival.error().code == EngineError::binding_conflict,
              "one constraint a target channel");
        check(scene.engine.constraint_count() == 1);
        take(scene.engine.unconstrain(first));
        check(scene.engine
                  .constrain({.kind = ConstraintKind::follow,
                              .target = {scene.tag, "tag"},
                              .source = {scene.arrow, "arrow"}})
                  .has_value());
        check(!scene.engine.unconstrain(first), "a removed constraint's handle is stale");
        check(!scene.engine.constrain({.kind = ConstraintKind::follow,
                                       .target = {scene.tag, "nothing"},
                                       .source = {scene.card, "card"}}));
    });

    suite.test("a source no playback shows is read as its object describes it", [] {
        Engine engine = take(Engine::make());
        const auto still = take(engine.create_object(single("still", {.tx = 40, .ty = 30})));
        const auto follower = take(engine.create_object(single("follower")));
        const auto shown = take(engine.play(anim::morph(follower, follower)));
        take(engine.constrain({.kind = ConstraintKind::follow,
                               .target = {follower, "follower"},
                               .source = {still, "still"}}));
        const auto frame = take(engine.frame(shown)).geometry;
        check(near(frame.parts[0].local.tx, 40) && near(frame.parts[0].local.ty, 30),
              "applied at once, from the description");
    });

    return suite.run(argc, argv);
}
