// The recipe library (L6): every recipe's whole course sampled — where it
// starts, its middle, where it ends — on a square standing at (100, 100):
// entrances end where it stood and fade in, exits leave from there and fade
// out, cues end where they began, loops go round `count` times, moves arrive.
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
    if (!result) std::abort();
    if constexpr (!std::is_void_v<T>) return std::move(*result);
}
ObjectDescription square(Vec2 at) {
    const std::array edges{ShapeSegment::line({20, -20}), ShapeSegment::line({20, 20}), ShapeSegment::line({-20, 20})};
    return {{{.id = "box",
              .local = {.tx = at.x, .ty = at.y},
              .contours = {take(Contour::copy({{-20, -20}, edges, true}))},
              .fill = Rgba{.2f, .5f, 1, 1},
              .stroke = PartStroke{Rgba{1, 1, 1, 1}, 2},
              .deformers = {deform::squash({0, 20})}}}};
}
struct Pose {
    double x = 0, y = 0, sx = 1, sy = 1, rotation = 0, opacity = 1, trim_start = 0, trim_end = 1;
};
struct Run {
    Engine engine = take(Engine::make());
    ObjectRef box = take(engine.create_object(square({100, 100})));
    PlaybackRef playback;
    double length = 0;
    explicit Run(const std::function<Program(ObjectRef)>& recipe) {
        playback = take(engine.play(recipe(box)));
        take(engine.pause(playback));
        length = take(engine.inspect(playback)).duration.count();
    }
    Pose at(double t, std::size_t track = 0) {
        take(engine.seek(playback, seconds(t)));
        const auto frame = take(engine.frame(playback, track));
        const auto& local = frame.geometry.parts[0].local;
        Pose p{local.tx, local.ty, local.sx, local.sy, local.rotate};
        if (!frame.effect_values.empty()) p.opacity = frame.effect_values[0];
        if (const auto v = frame.geometry.value(0, ch::trim_start)) p.trim_start = *v;
        if (const auto v = frame.geometry.value(0, ch::trim_end)) p.trim_end = *v;
        return p;
    }
};
bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool rests(const Pose& p) {
    return near(p.x, 100) && near(p.y, 100) && near(p.sx, 1) && near(p.sy, 1) && near(p.rotation, 0) && near(p.opacity, 1);
}
std::string text(const Pose& p) {
    return std::format("({:.3f}, {:.3f}) scale ({:.3f}, {:.3f}) turned {:.3f} opacity {:.3f} trim {:.2f}…{:.2f}", p.x, p.y, p.sx, p.sy, p.rotation,
                       p.opacity, p.trim_start, p.trim_end);
}
}  // namespace

int main(int argc, char** argv) {
    Suite s{"hanim.recipes"};

    s.test("entrances: from a pose of their own to where it stood, faded in", [] {
        struct Entrance {
            std::string_view name;
            std::function<Program(ObjectRef)> make;
            std::function<bool(const Pose&)> start;
        };
        const std::vector<Entrance> entrances{
            {"fade_in", [](ObjectRef o) { return anim::fade_in(o); }, [](const Pose& p) { return near(p.x, 100) && near(p.opacity, 0); }},
            {"rise_in", [](ObjectRef o) { return anim::rise_in(o); }, [](const Pose& p) { return near(p.y, 124) && near(p.opacity, 0); }},
            {"sink_in", [](ObjectRef o) { return anim::sink_in(o); }, [](const Pose& p) { return near(p.y, 76) && near(p.opacity, 0); }},
            {"slide_in", [](ObjectRef o) { return anim::slide_in(o, {-40, 10}); }, [](const Pose& p) { return near(p.x, 60) && near(p.y, 110); }},
            {"pop_in", [](ObjectRef o) { return anim::pop_in(o); }, [](const Pose& p) { return near(p.sx, .3) && near(p.opacity, 0); }},
            {"zoom_in", [](ObjectRef o) { return anim::zoom_in(o); }, [](const Pose& p) { return near(p.sx, 1.4) && near(p.opacity, 0); }},
            {"spin_in", [](ObjectRef o) { return anim::spin_in(o); }, [](const Pose& p) { return near(p.rotation, -1.2) && near(p.sx, .6); }},
            {"unfold_in", [](ObjectRef o) { return anim::unfold_in(o); }, [](const Pose& p) { return near(p.sx, 1) && near(p.sy, .05); }},
            {"write_in", [](ObjectRef o) { return anim::write_in(o, {.part = "box"}); }, [](const Pose& p) { return near(p.trim_end, 0); }},
        };
        for (const auto& e : entrances) {
            Run run(e.make);
            const Pose start = run.at(0), middle = run.at(run.length / 2), end = run.at(run.length);
            check(run.length > 0 && e.start(start), std::format("{} starts {}", e.name, text(start)));
            check(rests(end) && near(end.trim_end, 1), std::format("{} ends {}", e.name, text(end)));
            check(!rests(middle) || (e.name == "write_in" && middle.trim_end > 0 && middle.trim_end < 1), std::format("{} moves: {}", e.name, text(middle)));
        }
        // pop_in overshoots its size on the way (out_back).
        Run pop([](ObjectRef o) { return anim::pop_in(o); });
        double most = 0;
        for (int k = 0; k <= 40; ++k) most = std::max(most, pop.at(pop.length * k / 40).sx);
        check(most > 1.02, std::format("pop_in overshoots: {:.3f}", most));
        // drop_in falls from above onto where it stood, and rests there.
        Run drop([](ObjectRef o) { return anim::drop_in(o, 120, {.part = "box"}); });
        check(near(drop.at(0).y, -20) && near(drop.at(drop.length).y, 100), std::format("drop_in from {} to {}", drop.at(0).y, drop.at(drop.length).y));
    });

    s.test("exits: from where it stands to a pose of their own, faded out", [] {
        struct Exit {
            std::string_view name;
            std::function<Program(ObjectRef)> make;
            std::function<bool(const Pose&)> end;
        };
        const std::vector<Exit> exits{
            {"fade_out", [](ObjectRef o) { return anim::fade_out(o); }, [](const Pose& p) { return near(p.x, 100) && near(p.opacity, 0); }},
            {"rise_out", [](ObjectRef o) { return anim::rise_out(o); }, [](const Pose& p) { return near(p.y, 76) && near(p.opacity, 0); }},
            {"sink_out", [](ObjectRef o) { return anim::sink_out(o); }, [](const Pose& p) { return near(p.y, 124) && near(p.opacity, 0); }},
            {"slide_out", [](ObjectRef o) { return anim::slide_out(o, {50, 0}); }, [](const Pose& p) { return near(p.x, 150) && near(p.opacity, 0); }},
            {"pop_out", [](ObjectRef o) { return anim::pop_out(o); }, [](const Pose& p) { return near(p.sx, .3) && near(p.opacity, 0); }},
            {"zoom_out", [](ObjectRef o) { return anim::zoom_out(o); }, [](const Pose& p) { return near(p.sx, 1.4) && near(p.opacity, 0); }},
            {"spin_out", [](ObjectRef o) { return anim::spin_out(o); }, [](const Pose& p) { return near(p.rotation, 1.2) && near(p.sy, .6); }},
            {"fold_out", [](ObjectRef o) { return anim::fold_out(o); }, [](const Pose& p) { return near(p.sy, .05) && near(p.opacity, 0); }},
            {"unwrite", [](ObjectRef o) { return anim::unwrite(o, {.part = "box"}); }, [](const Pose& p) { return near(p.trim_start, 1); }},
        };
        for (const auto& e : exits) {
            Run run(e.make);
            const Pose start = run.at(0), end = run.at(run.length);
            check(run.length > 0 && rests(start) && near(start.trim_start, 0), std::format("{} starts {}", e.name, text(start)));
            check(e.end(end), std::format("{} ends {}", e.name, text(end)));
        }
    });

    s.test("attention: each cue does its own thing and ends where it began", [] {
        struct Cue {
            std::string_view name;
            std::function<Program(ObjectRef)> make;
            std::function<bool(const std::vector<Pose>&)> course;
        };
        const auto extreme = [](const std::vector<Pose>& poses, auto field, bool most) {
            double v = most ? -1e9 : 1e9;
            for (const auto& p : poses) v = most ? std::max(v, field(p)) : std::min(v, field(p));
            return v;
        };
        const std::vector<Cue> cues{
            {"pulse", [](ObjectRef o) { return anim::pulse(o); }, [&](const auto& c) { return near(extreme(c, [](const Pose& p) { return p.sx; }, true), 1.08, 1e-3); }},
            {"shake", [](ObjectRef o) { return anim::shake(o); }, [&](const auto& c) { return near(extreme(c, [](const Pose& p) { return p.x; }, false), 92, .05); }},
            {"wobble", [](ObjectRef o) { return anim::wobble(o); }, [&](const auto& c) { return near(extreme(c, [](const Pose& p) { return p.rotation; }, true), .12, 1e-3); }},
            {"bounce", [](ObjectRef o) { return anim::bounce(o, 30); }, [&](const auto& c) { return near(extreme(c, [](const Pose& p) { return p.y; }, false), 70, .05); }},
            {"flash", [](ObjectRef o) { return anim::flash(o, 2); }, [&](const auto& c) { return extreme(c, [](const Pose& p) { return p.opacity; }, false) < .02; }},
            {"heartbeat", [](ObjectRef o) { return anim::heartbeat(o, 1.2); }, [&](const auto& c) { return near(extreme(c, [](const Pose& p) { return p.sx; }, true), 1.2, 1e-3); }},
            {"rubber_band", [](ObjectRef o) { return anim::rubber_band(o); },
             [&](const auto& c) { return near(extreme(c, [](const Pose& p) { return p.sx; }, true), 1.25, 1e-3) && near(extreme(c, [](const Pose& p) { return p.sy; }, true), 1.25, 1e-3); }},
            {"swing", [](ObjectRef o) { return anim::swing(o, .3); },
             [&](const auto& c) { return near(extreme(c, [](const Pose& p) { return p.rotation; }, true), .3, 1e-3) && near(extreme(c, [](const Pose& p) { return p.rotation; }, false), -.198, 1e-3); }},
            {"tada", [](ObjectRef o) { return anim::tada(o); },
             [&](const auto& c) { return near(extreme(c, [](const Pose& p) { return p.sx; }, false), .9, 1e-3) && near(extreme(c, [](const Pose& p) { return p.sx; }, true), 1.1, 1e-3); }},
            {"nudge", [](ObjectRef o) { return anim::nudge(o, {10, 0}); }, [&](const auto& c) { return near(extreme(c, [](const Pose& p) { return p.x; }, true), 110, .05); }},
        };
        for (const auto& cue : cues) {
            Run run(cue.make);
            std::vector<Pose> course;
            for (int k = 0; k <= 280; ++k) course.push_back(run.at(run.length * k / 280));
            check(rests(course.front()) && rests(course.back()), std::format("{} begins {} and ends {}", cue.name, text(course.front()), text(course.back())));
            check(cue.course(course), std::format("{}'s course", cue.name));
        }
    });

    s.test("idle loops: `count` courses of `period`, each ending where it began", [] {
        const struct {
            std::string_view name;
            std::function<Program(ObjectRef)> make;
            std::function<bool(const Pose&)> halfway;
        } loops[]{
            {"bob", [](ObjectRef o) { return anim::bob(o, 6, seconds(2), 2); }, [](const Pose& p) { return near(p.y, 94); }},
            {"breathe", [](ObjectRef o) { return anim::breathe(o, 1.05, seconds(2), 2); }, [](const Pose& p) { return near(p.sx, 1.05); }},
            {"spin", [](ObjectRef o) { return anim::spin(o, seconds(2), 2); }, [](const Pose& p) { return near(std::fmod(p.rotation, 2 * std::numbers::pi), std::numbers::pi, 1e-6); }},
            {"sway", [](ObjectRef o) { return anim::sway(o, .1, seconds(2), 2); }, [](const Pose& p) { return near(p.rotation, 0, 1e-6); }},
            {"orbit", [](ObjectRef o) { return anim::orbit(o, 16, seconds(2), 2); }, [](const Pose& p) { return near(p.x, 100, .2) && near(p.y, 68, .2); }},
            {"blink", [](ObjectRef o) { return anim::blink(o, seconds(2), 2); }, [](const Pose& p) { return near(p.opacity, 0, 1e-3); }},
        };
        for (const auto& l : loops) {
            Run run(l.make);
            check(near(run.length, 4), std::format("{}: two courses of 2 s: {}", l.name, run.length));
            const Pose half = run.at(1), round = run.at(2), end = run.at(4);
            check(l.halfway(half), std::format("{} halfway round: {}", l.name, text(half)));
            Pose settled = end;
            settled.rotation = std::remainder(settled.rotation, 2 * std::numbers::pi);  // a whole turn is where it began
            check(rests(settled) && near(round.x, 100, 1e-6) && near(round.y, 100, 1e-6), std::format("{} round again: {}, ends {}", l.name, text(round), text(end)));
        }
        Run forever([](ObjectRef o) { return anim::spin(o); });
        check(std::isinf(forever.length), "spin, by default, forever");
    });

    s.test("moves: along an arc; two exchanging places, passing each other; several one after another", [] {
        Run arc([](ObjectRef o) { return anim::arc_to(o, {260, 100}, .3); });
        const Pose middle = arc.at(arc.length / 2), end = arc.at(arc.length);
        check(near(end.x, 260) && near(end.y, 100), std::format("arrives: {}", text(end)));
        check(std::abs(middle.y - 100) > 20, std::format("off the straight way halfway: {}", text(middle)));
        // Two objects.
        Engine engine = take(Engine::make());
        const auto a = take(engine.create_object(square({100, 100}))), b = take(engine.create_object(square({300, 100})));
        const auto played = take(engine.play(anim::swap(a, b, {200, 0})));
        take(engine.pause(played));
        const double length = take(engine.inspect(played)).duration.count();
        take(engine.seek(played, seconds(length / 2)));
        const double ya = take(engine.frame(played, 0)).geometry.parts[0].local.ty, yb = take(engine.frame(played, 1)).geometry.parts[0].local.ty;
        check((ya - 100) * (yb - 100) < 0, std::format("they pass on either side: {} and {}", ya, yb));
        take(engine.seek(played, seconds(length)));
        check(near(take(engine.frame(played, 0)).geometry.parts[0].local.tx, 300) && near(take(engine.frame(played, 1)).geometry.parts[0].local.tx, 100),
              "and exchange places");
        // Several, staggered.
        Engine many = take(Engine::make());
        std::vector<ObjectInput> objects;
        for (int k = 0; k < 3; ++k) objects.emplace_back(take(many.create_object(square({100.0 + 50 * k, 100}))));
        const std::array<Vec2, 3> offsets{Vec2{100, 0}, Vec2{-50, 0}, Vec2{-50, 0}};
        const auto shuffled = take(many.play(anim::reorder(objects, offsets, seconds(.1))));
        take(many.pause(shuffled));
        const double all = take(many.inspect(shuffled)).duration.count();
        check(near(all, .7, 1e-9), std::format("0.5 s each, 0.1 s apart: {}", all));
        take(many.seek(shuffled, seconds(.05)));
        check(take(many.frame(shuffled, 0)).geometry.parts[0].local.tx > 100 && near(take(many.frame(shuffled, 2)).geometry.parts[0].local.tx, 200),
              "the first moves before the last");
        take(many.seek(shuffled, seconds(all)));
        check(near(take(many.frame(shuffled, 0)).geometry.parts[0].local.tx, 200) && near(take(many.frame(shuffled, 2)).geometry.parts[0].local.tx, 150), "all arrive");
    });

    s.test("bad values are refused at preparation, not drawn", [] {
        Engine engine = take(Engine::make());
        const auto box = take(engine.create_object(square({100, 100})));
        for (const Program& bad : {anim::rise_in(box, std::numeric_limits<double>::infinity()), anim::fade_in(box, {.duration = seconds(-1)}),
                                   anim::flash(box, 0), anim::bob(box, 6, seconds(0)), anim::orbit(box, -4), anim::heartbeat(box, 0)})
            check(!engine.prepare(bad), "refused");
    });

    return s.run(argc, argv);
}
