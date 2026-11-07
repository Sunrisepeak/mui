// Interactive mode (L5): a driven channel is hanim.core's Set lanes on the
// engine's time — the same values, bit for bit; a retarget keeps position and
// velocity; lanes read one by one or all at once without allocating; refusals
// by kind; reduced motion shows targets.
import std;
import hanim.engine;
import mlibs.error;
import mlibs.testing;

using namespace hanim;
using namespace mlibs::testing;

namespace {
std::atomic<std::size_t> allocations{0};
}
void* operator new(std::size_t n) {
    allocations.fetch_add(1);
    if (void* p = std::malloc(n ? n : 1)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
inline constexpr auto opacity = ch::define<double>("ui.opacity", 0);
inline constexpr auto offset = ch::define<Vec2>("ui.offset");
inline constexpr auto offset_one = ch::define<double>("ui.offset");  // the same name, one lane
}  // namespace

int main(int argc, char** argv) {
    Suite s{"hanim.drive"};

    s.test("a driven channel is the set's lanes on the engine's time, bit for bit", [] {
        auto engine = *Engine::make();
        auto fade = *engine.drive(7, opacity);
        check_eq(*fade.value(), 0.0);
        check(fade.spring_to(1.0, springs::snappy).has_value());
        Set set;
        const auto lane = set.hold(0);
        set.spring_to(lane, seconds(0), 1.0, springs::snappy);
        for (int frame = 1; frame <= 30; ++frame) {
            check(engine.advance(seconds(1.0 / 60)).has_value());
            check_eq(*fade.value(), set.value(lane, engine.now()));
        }
        check(engine.now() == seconds(30.0 / 60) || std::abs(engine.now().count() - .5) < 1e-12, "the engine's time is the sum of its steps");
    });

    s.test("a retarget goes on from where the channel is and how fast it moves", [] {
        auto engine = *Engine::make();
        auto fade = *engine.drive(7, opacity);
        check(fade.spring_to(1.0).has_value() && engine.advance(seconds(.1)).has_value());
        const auto before = *fade.value();
        const auto speed = (*fade.velocity())[0];
        check(speed > 0, "moving toward 1");
        check(fade.spring_to(0.0).has_value());
        check_eq(*fade.value(), before);
        check_near((*fade.velocity())[0], speed, 1e-12 * std::abs(speed));  // the new spring starts at that velocity (to rounding)
        check(fade.tween_to(.5, seconds(.2), ease::linear).has_value() && engine.advance(seconds(.2)).has_value());
        check_eq(*fade.value(), .5);
        check(*fade.settled() && engine.moving() == 0, "a finished tween is settled");
        check(fade.jump(.25).has_value() && *fade.value() == .25, "a jump is there at once");
    });

    s.test("a channel's lanes are its type's; every lane is read at once without allocating", [] {
        auto engine = *Engine::make();
        auto drag = *engine.drive(3, offset);
        auto fade = *engine.drive(3, opacity);
        check(drag.spring_to({40, -20}).has_value() && fade.spring_to(1.0).has_value() && engine.advance(seconds(.05)).has_value());
        check_eq(engine.moving(), std::size_t{2});
        std::vector<double> out(engine.driven_lane_capacity());
        const auto before = allocations.load();
        const auto moving = engine.evaluate_driven(out);
        const auto value = drag.value();
        const auto opacity_now = fade.value();
        check_eq(allocations.load(), before);
        check(moving && *moving == 3, "three lanes moving");
        check(out[*drag.lane(0)] == value->x && out[*drag.lane(1)] == value->y && out[*fade.lane(0)] == *opacity_now, "the same values");
    });

    s.test("refusals: one lane count a key's channel, stale after release, non-finite targets", [] {
        auto engine = *Engine::make();
        auto drag = *engine.drive(3, offset);
        const auto other = engine.drive(3, offset_one);
        check(!other && other.error().kind() == mlibs::error::Kind::invalid_argument, "a name keeps its lanes");
        check(engine.drive(4, offset_one).has_value(), "another key has its own channels");
        check(!drag.spring_to({std::numeric_limits<double>::infinity(), 0}), "a non-finite target is refused");
        check(!drag.tween_to({1, 1}, seconds(-1)), "a negative duration is refused");
        check(drag.release().has_value());
        const auto gone = drag.value();
        check(!gone && gone.error().kind() == mlibs::error::Kind::stale, "released: stale");
        const auto again = engine.drive(3, offset);
        check(again && *again->value() == Vec2{}, "driven again from rest");
    });

    s.test("a timeline takes a driven channel over and hands it back with its momentum", [] {
        auto engine = *Engine::make();
        const auto contour = Contour::copy({{0, -1}, std::vector<ShapeSegment>{ShapeSegment::line({-1, 1}), ShapeSegment::line({1, 1})}, true});
        const auto card = *engine.create_object({{{.id = "card", .contours = {*contour}, .fill = Rgba{1, 0, 0, 1}}}});
        auto glow = *engine.drive(11, opacity);
        const auto playback = engine.play(anim::to(card, opacity = 1.0).duration(seconds(1)));
        check(playback && glow.follow(*playback).has_value());
        for (int frame = 0; frame < 30; ++frame) check(engine.advance(seconds(1.0 / 60)).has_value());
        check_near(*glow.value(), .5, 1e-12);
        check_near((*glow.velocity())[0], 1.0, 1e-9);
        for (int frame = 0; frame < 30; ++frame) check(engine.advance(seconds(1.0 / 60)).has_value());
        check_near(*glow.value(), 1.0, 1e-12);
        const auto handed = (*glow.velocity())[0];
        check_near(handed, 1.0, 1e-9);  // the timeline's last speed, kept
        check(engine.moving() == 1, "moving on with that momentum");
        check(glow.spring_to(0.0).has_value());
        check_near((*glow.velocity())[0], handed, 1e-9 * std::abs(handed));  // the next spring starts with it
        check(engine.advance(seconds(1.0 / 60)).has_value());
        Set reference;
        const auto lane = reference.spring(1.0, 0.0, seconds(0), springs::standard, handed);
        check_near(*glow.value(), reference.value(lane, seconds(1.0 / 60)), 1e-12);
    });

    s.test("a host's own clock: verbs at a time, values at any time the history reaches, a fling's glide", [] {
        auto engine = *Engine::make();
        auto x = *engine.drive(5, opacity);
        Set set;
        const auto lane = set.hold(0);
        // A transition that waits: it starts at 0.5 s of the host's time.
        check(x.tween_to(1.0, seconds(1), ease::linear, seconds(.5)).has_value());
        set.tween_to(lane, seconds(.5), 1.0, seconds(1), ease::linear);
        for (const double t : {0.0, .5, 1.0, 1.5})
            check_eq(*x.value(seconds(t)), set.value(lane, seconds(t)));
        check(*x.settled(seconds(1.5)) && !*x.settled(seconds(1)), "settled when the tween ends");
        check_eq(x.settle_time()->count(), 1.5);
        // A fling: from where it is, coasting to a stop it can say beforehand.
        check(x.glide(400.0, {}, seconds(2)).has_value());
        set.glide(lane, seconds(2), 400.0);
        check_eq(*x.target(), set.target(lane));
        check(*x.target() > 1.0 && *x.value(seconds(2.1)) == set.value(lane, seconds(2.1)), "moving on toward where it stops");
        check(!x.glide(400.0, {.rate = 1.5}), "a decay rate past 1 is refused");
    });

    s.test("under reduced motion a driven channel shows its target", [] {
        auto engine = *Engine::make();
        auto fade = *engine.drive(7, opacity);
        engine.reduce_motion(true);
        check(fade.spring_to(1.0).has_value());
        check(*fade.value() == 1.0 && engine.moving() == 0 && *fade.settled(), "there at once, nothing moving");
    });

    return s.run(argc, argv);
}
