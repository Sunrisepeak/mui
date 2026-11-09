// Tweens, keyframes, springs, decay and interruptible motion: the closed forms
// satisfy their equations, settle exactly, carry velocity across interruption,
// and give the same value however a time is reached.
import std;
import hanim.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using hanim::seconds;

namespace {

struct Sequence {
    std::uint64_t s;
    double next() {
        s += mlibs::math::golden;
        return mlibs::math::unit53(mlibs::math::finalize(s));
    }
};

// m·x'' + c·x' + k·(x − target) = 0, checked by central differences.
void satisfies_ode(const hanim::Oscillator& o, hanim::SpringParams p) {
    constexpr double h = 1e-5;
    for (double t = 0.01; t < std::min(o.settle_time(), 2.0) - 0.01; t += 0.037) {
        double x = o.position(t);
        double v = (o.position(t + h) - o.position(t - h)) / (2 * h);
        double a = (o.position(t + h) - 2 * x + o.position(t - h)) / (h * h);
        double residual = p.mass * a + p.damping * v + p.stiffness * (x - o.target());
        expect(std::abs(residual) < 1e-2 * (1 + p.stiffness)) << std::format("residual {} at t = {}", residual, t);
        expect(near(o.velocity(t), v, 1e-5 * (1 + std::abs(v))));
    }
}

}  // namespace

UTest cases{"hanim.motion", [] {

    "tween holds its ends and follows its ease"_test = [] {
        hanim::Tween<double> t{.from = 10, .to = 20, .duration = seconds(2), .ease = hanim::ease::linear,
                              .delay = seconds(1)};
        expect(eq(std::bit_cast<std::uint64_t>(double(t.value(seconds(0)))), std::bit_cast<std::uint64_t>(double(10))));
        expect(eq(std::bit_cast<std::uint64_t>(double(t.value(seconds(1)))), std::bit_cast<std::uint64_t>(double(10))));
        expect(eq(std::bit_cast<std::uint64_t>(double(t.value(seconds(2)))), std::bit_cast<std::uint64_t>(double(15))));
        expect(eq(std::bit_cast<std::uint64_t>(double(t.value(seconds(3)))), std::bit_cast<std::uint64_t>(double(20))));
        expect(eq(std::bit_cast<std::uint64_t>(double(t.value(seconds(9)))), std::bit_cast<std::uint64_t>(double(20))));
        expect(near(t.velocity(seconds(2))[0], 5.0, 1e-9));
    };

    "keyframes: ease per segment, hold, cut"_test = [] {
        hanim::Track<double> tr;
        tr.key(seconds(0), 0.0, hanim::ease::linear)
            .key(seconds(1), 100.0, hanim::ease::in_quad)
            .hold(seconds(2), 50.0)
            .key(seconds(3), 80.0)
            .key(seconds(3), 10.0)  // a cut at 3 s
            .key(seconds(4), 20.0);
        expect(eq(std::bit_cast<std::uint64_t>(double(tr.value(seconds(-1)))), std::bit_cast<std::uint64_t>(double(0.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(tr.value(seconds(0.5)))), std::bit_cast<std::uint64_t>(double(50.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(tr.value(seconds(1.5)))), std::bit_cast<std::uint64_t>(double(100.0 + (50.0 - 100.0) * 0.25))));
        expect(eq(std::bit_cast<std::uint64_t>(double(tr.value(seconds(2.5)))), std::bit_cast<std::uint64_t>(double(50.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(tr.value(seconds(3)))), std::bit_cast<std::uint64_t>(double(10.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(tr.value(seconds(3.5)))), std::bit_cast<std::uint64_t>(double(15.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(tr.value(seconds(10)))), std::bit_cast<std::uint64_t>(double(20.0))));
        expect(tr.keys().size() == 6);
    };

    "oscillator closed forms satisfy the spring equation"_test = [] {
        for (auto p : {hanim::springs::wobbly, hanim::springs::standard, hanim::springs::molasses,
                       hanim::SpringParams{100, 20, 1}, hanim::SpringParams::response(0.5, 0.3)}) {
            hanim::Oscillator o(0, 3, 100, p);
            satisfies_ode(o, p);
            expect(eq(std::bit_cast<std::uint64_t>(double(o.position(0))), std::bit_cast<std::uint64_t>(double(0))));
            expect(near(o.velocity(0), 3, 1e-9));
        }
    };

    "springs settle exactly, and settle later when bouncier"_test = [] {
        hanim::Oscillator calm(0, 0, 1, hanim::springs::stiff);
        hanim::Oscillator bouncy(0, 0, 1, hanim::springs::wobbly);
        expect(calm.settle_time() > 0 && calm.settle_time() < 5);
        expect(bouncy.settle_time() > calm.settle_time() * 0.5);
        expect(eq(std::bit_cast<std::uint64_t>(double(calm.position(calm.settle_time()))), std::bit_cast<std::uint64_t>(double(1.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(calm.velocity(calm.settle_time()))), std::bit_cast<std::uint64_t>(double(0.0))));
        // Just before settling it is within the rest thresholds.
        double before = calm.settle_time() - 1e-6;
        expect(std::abs(calm.position(before) - 1.0) <= 1e-3 + 1e-6);
        // Underdamped springs overshoot.
        double peak = 0;
        for (double t = 0; t < bouncy.settle_time(); t += 0.005) peak = std::max(peak, bouncy.position(t));
        expect(peak > 1.0);
        // Already at rest: settled immediately.
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::Oscillator(1, 0, 1, hanim::springs::standard).settle_time())), std::bit_cast<std::uint64_t>(double(0.0))));
    };

    "spring of a vector settles all lanes together"_test = [] {
        hanim::Spring<hanim::Vec2> sp{.from = {0, 0}, .to = {100, -40}, .params = hanim::springs::gentle};
        auto end = sp.settle_time();
        expect(sp.value(end) == hanim::Vec2{100, -40});
        expect(sp.value(seconds(0)) == hanim::Vec2{0, 0});
        auto mid = sp.value(end * 0.3);
        expect(mid.x > 0 && mid.y < 0);
    };

    "decay glides to its destination"_test = [] {
        hanim::Glide g(0, 1000, {});
        expect(near(g.destination(), 1000 * (-1.0 / (1000 * std::log(0.998))), 1e-6));
        expect(eq(std::bit_cast<std::uint64_t>(double(g.position(g.settle_time()))), std::bit_cast<std::uint64_t>(double(g.destination()))));
        expect(g.position(0.1) > 0 && g.position(0.1) < g.destination());
        expect(g.velocity(0.1) < 1000 && g.velocity(0.1) > 0);
        hanim::Decay<double> d{.from = 5, .velocity = {200}};
        expect(eq(std::bit_cast<std::uint64_t>(double(d.value(seconds(100)))), std::bit_cast<std::uint64_t>(double(d.destination()))));
    };

    "an interrupted spring keeps position and velocity"_test = [] {
        hanim::Motion<double> m(0.0);
        m.spring_to(seconds(0), 100, hanim::springs::wobbly);
        double t = 0.137;
        double x_before = m.value(seconds(t));
        double v_before = m.velocity(seconds(t))[0];
        m.spring_to(seconds(t), -50, hanim::springs::stiff);
        expect(eq(std::bit_cast<std::uint64_t>(double(m.value(seconds(t)))), std::bit_cast<std::uint64_t>(double(x_before))));
        expect(std::abs(m.velocity(seconds(t))[0] - v_before) < 1e-9) << "velocity is continuous";
        expect(m.segments() == 2);
        // History before the interruption is unchanged.
        hanim::Motion<double> ref(0.0);
        ref.spring_to(seconds(0), 100, hanim::springs::wobbly);
        expect(eq(std::bit_cast<std::uint64_t>(double(m.value(seconds(0.1)))), std::bit_cast<std::uint64_t>(double(ref.value(seconds(0.1))))));
        expect(eq(std::bit_cast<std::uint64_t>(double(m.value(m.settle_time(seconds(1))))), std::bit_cast<std::uint64_t>(double(-50.0))));
    };

    "a tween interrupted by a spring carries its velocity"_test = [] {
        hanim::Motion<double> m(0.0);
        m.tween_to(seconds(0), 10, seconds(1), hanim::ease::linear);
        m.spring_to(seconds(0.5), 10, hanim::springs::standard);
        expect(near(m.velocity(seconds(0.5))[0], 10.0, 1e-6));
        expect(near(m.value(seconds(0.5)), 5.0, 1e-12));
    };

    "seeking in any order gives the same values"_test = [] {
        hanim::Motion<hanim::Vec2> m({0, 0});
        m.spring_to(seconds(0), {300, 200}, hanim::springs::wobbly)
            .tween_to(seconds(0.4), {-100, 50}, seconds(0.5), hanim::ease::in_out_cubic)
            .decay(seconds(1.1), {800, -300})
            .spring_to(seconds(2.0), {0, 0}, hanim::springs::gentle);
        Sequence seq{7};
        std::vector<double> times;
        for (int i = 0; i < 1000; ++i) times.push_back(seq.next() * 6.0);
        std::vector<hanim::Vec2> forward;
        auto sorted = times;
        std::ranges::sort(sorted);
        std::map<double, hanim::Vec2> by_time;
        for (double t : sorted) by_time[t] = m.value(seconds(t));
        for (double t : times) {
            auto v = m.value(seconds(t));
            expect(v == by_time[t]) << std::format("t = {}", t);
        }
    };

    "starting a motion in the past branches history"_test = [] {
        hanim::Motion<double> m(0.0);
        m.spring_to(seconds(0), 1).spring_to(seconds(1), 2).spring_to(seconds(2), 3);
        expect(m.segments() == 3);
        m.tween_to(seconds(1.5), 9, seconds(0.1));
        expect(m.segments() == 3) << "the motion at 2 s is discarded, the one at 1.5 s added";
        expect(eq(std::bit_cast<std::uint64_t>(double(m.value(seconds(5)))), std::bit_cast<std::uint64_t>(double(9.0))));
        m.forget_before(seconds(1.6));
        expect(m.segments() == 1);
        expect(eq(std::bit_cast<std::uint64_t>(double(m.value(seconds(5)))), std::bit_cast<std::uint64_t>(double(9.0))));
    };

    "jump sets a value at rest"_test = [] {
        hanim::Motion<double> m(0.0);
        m.spring_to(seconds(0), 10).jump(seconds(0.2), 42);
        expect(eq(std::bit_cast<std::uint64_t>(double(m.value(seconds(0.2)))), std::bit_cast<std::uint64_t>(double(42.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(m.value(seconds(3)))), std::bit_cast<std::uint64_t>(double(42.0))));
        expect(m.settled(seconds(0.2)));
    };

    "interpolate maps ranges like video authoring expects"_test = [] {
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::interpolate(15, {0, 30}, {0, 1}))), std::bit_cast<std::uint64_t>(double(0.5))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::interpolate(-5, {0, 30}, {0, 1}))), std::bit_cast<std::uint64_t>(double(0.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::interpolate(45, {0, 30}, {0, 1}))), std::bit_cast<std::uint64_t>(double(1.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::interpolate(45, {0, 30}, {0, 1},
                                     {.right = hanim::Extrapolate::extend}))), std::bit_cast<std::uint64_t>(double(1.5))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::interpolate(45, {0, 30}, {0, 1}, {.right = hanim::Extrapolate::identity}))), std::bit_cast<std::uint64_t>(double(45.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::interpolate(20, {0, 10, 30}, {0, 100, 0}))), std::bit_cast<std::uint64_t>(double(50.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::interpolate(5, {0, 10}, {0, 100}, {.ease = hanim::ease::in_quad}))), std::bit_cast<std::uint64_t>(double(25.0))));
    };

    "integers round half away from zero"_test = [] {
        expect(eq(hanim::lerp(0, 10, 0.25), 3));
        expect(eq(hanim::lerp(0, -10, 0.25), -3));
        expect(eq(hanim::lerp(0, 10, 0.24), 2));
    };
}};
