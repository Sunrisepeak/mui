// Time mapping (L4): a program's one course played through hanim.core's
// TimeMap — replayed (not accumulated), alternating, reversed, at a rate,
// forever — sampled at any time, played by the engine; markers name instants.
import std;
import hanim.engine;
import mlibs.error;
import mlibs.testing;

using namespace hanim;
using namespace mlibs::testing;

namespace {
ObjectDescription shape() {
    const auto contour = Contour::copy({{0, -1}, std::vector<ShapeSegment>{ShapeSegment::line({-1, 1}), ShapeSegment::line({1, 1})}, true});
    return {{{.id = "shape", .contours = {*contour}, .fill = Rgba{1, 0, 0, 1}}}};
}
struct Fixture {
    Engine    engine = *Engine::make();
    ObjectRef a = *engine.create_object(shape());
    PreparedProgram prepare(const Program& p) {
        auto r = engine.prepare(p);
        check(r.has_value(), r ? std::string() : r.error().text());
        if (!r) std::abort();
        return *r;
    }
};
double x_at(const PreparedProgram& p, double t) {
    std::vector<FramePart> parts(p.tracks()[0].requirements.parts);
    std::vector<ShapeCubic> cubics(p.tracks()[0].requirements.cubics);
    std::vector<float> effects(p.tracks()[0].max_effect_parameters);
    const auto s = p.sample(0, seconds(t), {parts, cubics}, effects);
    check(s.has_value(), s ? "" : s.error().text());
    return s ? s->geometry.parts[0].local.tx : std::numeric_limits<double>::quiet_NaN();
}
}  // namespace

int main(int argc, char** argv) {
    Suite s{"hanim.time-map"};

    s.test("a loop replays its one course; a repeat goes on from where it ended", [] {
        Fixture f;
        const auto move = anim::move_by(f.a, {10, 0}).duration(seconds(1));
        const auto looped = f.prepare(loop(move, 3));
        check_eq(looped.duration().count(), 3.0);
        check_eq(looped.natural_duration().count(), 1.0);
        for (const auto& [t, x] : std::array<std::pair<double, double>, 5>{{{0, 0}, {.5, 5}, {1.5, 5}, {2.25, 2.5}, {3, 10}}})
            check_eq(x_at(looped, t), x);
        const auto repeated = f.prepare(repeat(3, move));
        check_eq(x_at(repeated, 1.5), 15.0);
    });

    s.test("alternate plays every other course backwards; reverse plays each; rate scales time", [] {
        Fixture f;
        const auto move = anim::move_by(f.a, {10, 0}).duration(seconds(1));
        const auto ping = f.prepare(loop(move, 2, true));
        for (const auto& [t, x] : std::array<std::pair<double, double>, 4>{{{.5, 5}, {1.25, 7.5}, {1.5, 5}, {2, 0}}})
            check_eq(x_at(ping, t), x);
        const auto back = f.prepare(timed(move, {.reverse = true}));
        check_eq(x_at(back, .25), 7.5);
        const auto fast = f.prepare(timed(move, {.rate = 2}));
        check_eq(fast.duration().count(), .5);
        check_eq(x_at(fast, .25), 5.0);
    });

    s.test("forever: any time samples its course; the engine plays it without end", [] {
        Fixture f;
        const auto spin = loop(anim::move_by(f.a, {10, 0}).duration(seconds(1)));
        const auto p = f.prepare(spin);
        check(std::isinf(p.duration().count()), "a loop without a count has no end");
        check_eq(x_at(p, 1000.25), 2.5);
        const auto ref = f.engine.play(spin);
        check(ref && f.engine.advance(seconds(10.25)).has_value());
        const auto frame = f.engine.frame(*ref);
        check(frame && frame->geometry.parts[0].local.tx == 2.5, "ten and a quarter courses in: a quarter of the way");
        check(f.engine.inspect(*ref)->state == PlaybackState::playing && f.engine.inspect(*ref)->progress == .25, "still playing, a quarter through");
        const auto seek = f.engine.seek_progress(*ref, .5);
        check(!seek && seek.error().kind() == mlibs::error::Kind::refused, "no fraction of forever");
    });

    s.test("inside a program a loop unrolls into courses that replay the first, forwards or backwards", [] {
        Fixture f;
        const auto move = anim::move_by(f.a, {10, 0}).duration(seconds(1));
        const auto step = anim::move_by(f.a, {1, 0}).duration(seconds(1));
        const auto looped = f.prepare(sequence(move, loop(step, 2)));
        check_eq(looped.duration().count(), 3.0);
        // At a boundary the next course has begun (later starts win), from the first course's start.
        for (const auto& [t, x] : std::array<std::pair<double, double>, 4>{{{1.5, 10.5}, {2, 10}, {2.5, 10.5}, {3, 11}}})
            check_eq(x_at(looped, t), x);
        const auto ping = f.prepare(sequence(move, loop(step, 3, true)));
        for (const auto& [t, x] : std::array<std::pair<double, double>, 5>{{{1.5, 10.5}, {2.25, 10.75}, {2.5, 10.5}, {3, 10}, {3.5, 10.5}}})
            check_eq(x_at(ping, t), x);
        // What follows goes on from where the last course left the object.
        const auto after = f.prepare(sequence(loop(move, 2, true), step));
        check_eq(x_at(after, 2), 0.0);
        check_eq(x_at(after, 3), 1.0);
        const auto backwards = f.prepare(sequence(step, timed(move, {.reverse = true})));
        check_eq(x_at(backwards, 1.25), 8.5);   // from 1 + 10 back toward 1
        check_eq(x_at(backwards, 2), 1.0);
        const auto nested = f.prepare(loop(sequence(loop(step, 2, true), move), 2));
        check_eq(nested.duration().count(), 6.0);
        check_eq(x_at(nested, 0.5), .5);
        check_eq(x_at(nested, 1.5), .5);
        check_eq(x_at(nested, 2.5), 5.0);
        check_eq(x_at(nested, 3.5), .5);  // the outer loop replays from its own start
    });

    s.test("time on the expression: loop, alternate, reverse and rate are the same time maps", [] {
        Fixture f;
        const Program move = anim::move_by(f.a, {10, 0}).duration(seconds(1));
        const auto ping = f.prepare(move.loop(2).alternate());
        check(ping.time_map() && ping.time_map()->repeat == 2 && ping.time_map()->alternate, "one map, both settings");
        check_eq(x_at(ping, 1.25), 7.5);
        const auto fast = f.prepare(move.rate(2).rate(2));
        check_eq(fast.duration().count(), .25);
        const auto back = f.prepare(move.reverse());
        check_eq(x_at(back, .25), 7.5);
        const auto inner = f.prepare(sequence(move, move.loop(2).alternate()));
        check_eq(x_at(inner, 2.5), 15.0);  // the second course backwards: from 20 toward 10
    });

    s.test("rates, trims and endless loops map a whole program only; markers name instants", [] {
        Fixture f;
        const auto move = anim::move_by(f.a, {10, 0}).duration(seconds(1));
        for (const TimeMap m : {TimeMap{.rate = 2}, TimeMap{.trim = seconds(.5)}, TimeMap{.repeat = 1.5},
                                TimeMap{.repeat = std::numeric_limits<double>::infinity()}}) {
            const auto nested = f.engine.prepare(sequence(move, timed(anim::move_by(f.a, {1, 0}), m)));
            check(!nested && nested.error().caused_by(ProgramError::nested_time_map) && nested.error().kind() == mlibs::error::Kind::refused,
                  nested ? "accepted" : nested.error().text());
        }
        const auto bad = f.engine.prepare(timed(move, {.rate = 0}));
        check(!bad && bad.error().kind() == mlibs::error::Kind::invalid_argument, "a rate of zero is refused");
        const auto cued = f.prepare(sequence(move, marker("hit"), move));
        check(cued.markers().size() == 1 && cued.markers()[0].name == "hit" && cued.markers()[0].time == seconds(1), "the hit at one second");
        check_eq(x_at(cued, 1.5), 15.0);
    });

    return s.run(argc, argv);
}
