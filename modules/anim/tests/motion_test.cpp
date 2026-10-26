// Tweens, keyframes, springs, decay and interruptible motion: the closed forms
// satisfy their equations, settle exactly, carry velocity across interruption,
// and give the same value however a time is reached.
import std;
import openmui.anim;
import openmui.testing;

using namespace omui::testing;
namespace anim = omui::anim;
using anim::seconds;

namespace {

struct Sequence {
    std::uint64_t s;
    double next() {
        std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) * 0x1.0p-53;
    }
};

// m·x'' + c·x' + k·(x − target) = 0, checked by central differences.
void satisfies_ode(const anim::Oscillator& o, anim::SpringParams p) {
    constexpr double h = 1e-5;
    for (double t = 0.01; t < std::min(o.settle_time(), 2.0) - 0.01; t += 0.037) {
        double x = o.position(t);
        double v = (o.position(t + h) - o.position(t - h)) / (2 * h);
        double a = (o.position(t + h) - 2 * x + o.position(t - h)) / (h * h);
        double residual = p.mass * a + p.damping * v + p.stiffness * (x - o.target());
        check(std::abs(residual) < 1e-2 * (1 + p.stiffness), std::format("residual {} at t = {}", residual, t));
        check_near(o.velocity(t), v, 1e-5 * (1 + std::abs(v)));
    }
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"anim.motion"};

    s.test("tween holds its ends and follows its ease", [] {
        anim::Tween<double> t{.from = 10, .to = 20, .duration = seconds(2), .ease = anim::ease::linear,
                              .delay = seconds(1)};
        check_bits(t.value(seconds(0)), 10);
        check_bits(t.value(seconds(1)), 10);
        check_bits(t.value(seconds(2)), 15);
        check_bits(t.value(seconds(3)), 20);
        check_bits(t.value(seconds(9)), 20);
        check_near(t.velocity(seconds(2))[0], 5.0, 1e-9);
    });

    s.test("keyframes: ease per segment, hold, cut", [] {
        anim::Track<double> tr;
        tr.key(seconds(0), 0.0, anim::ease::linear)
            .key(seconds(1), 100.0, anim::ease::in_quad)
            .hold(seconds(2), 50.0)
            .key(seconds(3), 80.0)
            .key(seconds(3), 10.0)  // a cut at 3 s
            .key(seconds(4), 20.0);
        check_bits(tr.value(seconds(-1)), 0.0);
        check_bits(tr.value(seconds(0.5)), 50.0);
        check_bits(tr.value(seconds(1.5)), 100.0 + (50.0 - 100.0) * 0.25);
        check_bits(tr.value(seconds(2.5)), 50.0);
        check_bits(tr.value(seconds(3)), 10.0);
        check_bits(tr.value(seconds(3.5)), 15.0);
        check_bits(tr.value(seconds(10)), 20.0);
        check(tr.keys().size() == 6);
    });

    s.test("oscillator closed forms satisfy the spring equation", [] {
        for (auto p : {anim::springs::wobbly, anim::springs::standard, anim::springs::molasses,
                       anim::SpringParams{100, 20, 1}, anim::SpringParams::response(0.5, 0.3)}) {
            anim::Oscillator o(0, 3, 100, p);
            satisfies_ode(o, p);
            check_bits(o.position(0), 0);
            check_near(o.velocity(0), 3, 1e-9);
        }
    });

    s.test("springs settle exactly, and settle later when bouncier", [] {
        anim::Oscillator calm(0, 0, 1, anim::springs::stiff);
        anim::Oscillator bouncy(0, 0, 1, anim::springs::wobbly);
        check(calm.settle_time() > 0 && calm.settle_time() < 5);
        check(bouncy.settle_time() > calm.settle_time() * 0.5);
        check_bits(calm.position(calm.settle_time()), 1.0);
        check_bits(calm.velocity(calm.settle_time()), 0.0);
        // Just before settling it is within the rest thresholds.
        double before = calm.settle_time() - 1e-6;
        check(std::abs(calm.position(before) - 1.0) <= 1e-3 + 1e-6);
        // Underdamped springs overshoot.
        double peak = 0;
        for (double t = 0; t < bouncy.settle_time(); t += 0.005) peak = std::max(peak, bouncy.position(t));
        check(peak > 1.0);
        // Already at rest: settled immediately.
        check_bits(anim::Oscillator(1, 0, 1, anim::springs::standard).settle_time(), 0.0);
    });

    s.test("spring of a vector settles all lanes together", [] {
        anim::Spring<anim::Vec2> sp{.from = {0, 0}, .to = {100, -40}, .params = anim::springs::gentle};
        auto end = sp.settle_time();
        check(sp.value(end) == anim::Vec2{100, -40});
        check(sp.value(seconds(0)) == anim::Vec2{0, 0});
        auto mid = sp.value(end * 0.3);
        check(mid.x > 0 && mid.y < 0);
    });

    s.test("decay glides to its destination", [] {
        anim::Glide g(0, 1000, {});
        check_near(g.destination(), 1000 * (-1.0 / (1000 * std::log(0.998))), 1e-6);
        check_bits(g.position(g.settle_time()), g.destination());
        check(g.position(0.1) > 0 && g.position(0.1) < g.destination());
        check(g.velocity(0.1) < 1000 && g.velocity(0.1) > 0);
        anim::Decay<double> d{.from = 5, .velocity = {200}};
        check_bits(d.value(seconds(100)), d.destination());
    });

    s.test("an interrupted spring keeps position and velocity", [] {
        anim::Motion<double> m(0.0);
        m.spring_to(seconds(0), 100, anim::springs::wobbly);
        double t = 0.137;
        double x_before = m.value(seconds(t));
        double v_before = m.velocity(seconds(t))[0];
        m.spring_to(seconds(t), -50, anim::springs::stiff);
        check_bits(m.value(seconds(t)), x_before);
        check(std::abs(m.velocity(seconds(t))[0] - v_before) < 1e-9, "velocity is continuous");
        check(m.segments() == 2);
        // History before the interruption is unchanged.
        anim::Motion<double> ref(0.0);
        ref.spring_to(seconds(0), 100, anim::springs::wobbly);
        check_bits(m.value(seconds(0.1)), ref.value(seconds(0.1)));
        check_bits(m.value(m.settle_time(seconds(1))), -50.0);
    });

    s.test("a tween interrupted by a spring carries its velocity", [] {
        anim::Motion<double> m(0.0);
        m.tween_to(seconds(0), 10, seconds(1), anim::ease::linear);
        m.spring_to(seconds(0.5), 10, anim::springs::standard);
        check_near(m.velocity(seconds(0.5))[0], 10.0, 1e-6);
        check_near(m.value(seconds(0.5)), 5.0, 1e-12);
    });

    s.test("seeking in any order gives the same values", [] {
        anim::Motion<anim::Vec2> m({0, 0});
        m.spring_to(seconds(0), {300, 200}, anim::springs::wobbly)
            .tween_to(seconds(0.4), {-100, 50}, seconds(0.5), anim::ease::in_out_cubic)
            .decay(seconds(1.1), {800, -300})
            .spring_to(seconds(2.0), {0, 0}, anim::springs::gentle);
        Sequence seq{7};
        std::vector<double> times;
        for (int i = 0; i < 1000; ++i) times.push_back(seq.next() * 6.0);
        std::vector<anim::Vec2> forward;
        auto sorted = times;
        std::ranges::sort(sorted);
        std::map<double, anim::Vec2> by_time;
        for (double t : sorted) by_time[t] = m.value(seconds(t));
        for (double t : times) {
            auto v = m.value(seconds(t));
            check(v == by_time[t], std::format("t = {}", t));
        }
    });

    s.test("starting a motion in the past branches history", [] {
        anim::Motion<double> m(0.0);
        m.spring_to(seconds(0), 1).spring_to(seconds(1), 2).spring_to(seconds(2), 3);
        check(m.segments() == 3);
        m.tween_to(seconds(1.5), 9, seconds(0.1));
        check(m.segments() == 3, "the motion at 2 s is discarded, the one at 1.5 s added");
        check_bits(m.value(seconds(5)), 9.0);
        m.forget_before(seconds(1.6));
        check(m.segments() == 1);
        check_bits(m.value(seconds(5)), 9.0);
    });

    s.test("jump sets a value at rest", [] {
        anim::Motion<double> m(0.0);
        m.spring_to(seconds(0), 10).jump(seconds(0.2), 42);
        check_bits(m.value(seconds(0.2)), 42.0);
        check_bits(m.value(seconds(3)), 42.0);
        check(m.settled(seconds(0.2)));
    });

    s.test("interpolate maps ranges like video authoring expects", [] {
        check_bits(anim::interpolate(15, {0, 30}, {0, 1}), 0.5);
        check_bits(anim::interpolate(-5, {0, 30}, {0, 1}), 0.0);
        check_bits(anim::interpolate(45, {0, 30}, {0, 1}), 1.0);
        check_bits(anim::interpolate(45, {0, 30}, {0, 1},
                                     {.right = anim::Extrapolate::extend}),
                   1.5);
        check_bits(anim::interpolate(45, {0, 30}, {0, 1}, {.right = anim::Extrapolate::identity}), 45.0);
        check_bits(anim::interpolate(20, {0, 10, 30}, {0, 100, 0}), 50.0);
        check_bits(anim::interpolate(5, {0, 10}, {0, 100}, {.ease = anim::ease::in_quad}), 25.0);
    });

    s.test("integers round half away from zero", [] {
        check_eq(anim::lerp(0, 10, 0.25), 3);
        check_eq(anim::lerp(0, -10, 0.25), -3);
        check_eq(anim::lerp(0, 10, 0.24), 2);
    });

    return s.run(argc, argv);
}
