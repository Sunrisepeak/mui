import std;
import hanim;
import openmui.testing;

using namespace omui::testing;
using hanim::seconds;

int main(int argc, char** argv) {
    Suite s{"hanim.set"};

    s.test("evaluate agrees with per-channel values", [] {
        hanim::Set set;
        auto a = set.tween(0, 10, seconds(0), seconds(1), hanim::ease::linear);
        auto b = set.spring(0, 100, seconds(0), hanim::springs::wobbly);
        auto c = set.decay(0, 500, seconds(0));
        auto d = set.hold(7);
        std::vector<double> out(set.capacity());
        for (double t = 0; t < 3; t += 0.1) {
            auto moving = hanim::evaluate(set, seconds(t), out);
            check_bits(out[a.index], set.value(a, seconds(t)));
            check_bits(out[b.index], set.value(b, seconds(t)));
            check_bits(out[c.index], set.value(c, seconds(t)));
            check_bits(out[d.index], 7.0);
            check(moving <= 3);
        }
        check(hanim::evaluate(set, seconds(100), out) == 0, "all at rest eventually");
    });

    s.test("retargeting a spring keeps position and velocity", [] {
        hanim::Set set;
        auto ch = set.spring(0, 100, seconds(0), hanim::springs::gentle);
        double t = 0.2;
        double x = set.value(ch, seconds(t));
        double v = set.velocity(ch, seconds(t));
        set.spring_to(ch, seconds(t), -20);
        check_bits(set.value(ch, seconds(t)), x);
        check(std::abs(set.velocity(ch, seconds(t)) - v) < 1e-9);
        check_bits(set.value(ch, set.settle_time(ch)), -20.0);
        check_bits(set.target(ch), -20.0);
    });

    s.test("seek consistency: 1000 random frames, seeked after the fact, are the frames played (M2 ③)", [] {
        // Played: a spring interrupted, retargeted, turned into a tween and back,
        // stepped at 60 Hz with every interruption applied when its time comes.
        struct Step {
            double t;
            int    kind;  // 0: spring_to, 1: tween_to
            double target;
        };
        const std::array<Step, 4> script{{{0.25, 0, -20}, {0.6, 1, 50}, {1.1, 0, 0}, {1.45, 0, 80}}};
        hanim::Set set;
        const auto ch = set.spring(0, 100, seconds(0), hanim::springs::gentle);
        std::vector<double> played;
        std::size_t next = 0;
        constexpr int frames = 60 * 4;
        for (int f = 0; f <= frames; ++f) {
            const double t = f / 60.0;
            while (next < script.size() && script[next].t <= t) {
                const Step& st = script[next++];
                const double before = set.velocity(ch, seconds(st.t));
                if (st.kind == 0) set.spring_to(ch, seconds(st.t), st.target, st.target == 0 ? std::optional(hanim::springs::wobbly) : std::nullopt);
                else set.tween_to(ch, seconds(st.t), st.target, seconds(0.4), hanim::ease::in_out_cubic);
                if (st.kind == 0)
                    check(std::abs(set.velocity(ch, seconds(st.t)) - before) < 1e-9, std::format("velocity continuous at {}", st.t));
            }
            played.push_back(set.value(ch, seconds(t)));
        }
        // Seeked: the finished channel asked about any time — its segments are its history.
        std::uint64_t state = 20261027;
        auto next_random = [&] {
            state += 0x9e3779b97f4a7c15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        };
        int differing = 0;
        for (int i = 0; i < 1000; ++i) {
            const auto f = static_cast<int>(next_random() % (frames + 1));
            if (std::bit_cast<std::uint64_t>(set.value(ch, seconds(f / 60.0))) != std::bit_cast<std::uint64_t>(played[static_cast<std::size_t>(f)]))
                ++differing;
        }
        check_eq(differing, 0);
    });

    s.test("reduced motion shows targets", [] {
        hanim::Set set;
        auto ch = set.tween(0, 1, seconds(0), seconds(1));
        check_bits(set.value(ch, seconds(0.1), {.reduce_motion = true}), 1.0);
    });

    s.test("removed channels are invalid and their slots reused", [] {
        hanim::Set set;
        auto a = set.hold(1);
        set.remove(a);
        check(!set.valid(a));
        auto b = set.hold(2);
        check(b.index == a.index && b.generation != a.generation);
        check(set.size() == 1);
        set.clear();
        check(set.size() == 0 && !set.valid(b));
    });

    s.test("evaluating does not allocate", [] {
        hanim::Set set;
        for (int i = 0; i < 1000; ++i) (void)set.spring(0, i, seconds(i * 0.001), hanim::springs::standard);
        std::vector<double> out(set.capacity());
        // A timing smoke test rather than an allocation count (no allocator
        // hook in a std-only package): 1000 springs in well under a frame.
        auto start = std::chrono::steady_clock::now();
        for (int f = 0; f < 60; ++f) (void)hanim::evaluate(set, seconds(f / 60.0), out);
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 60;
        std::println("    1000 springs: {:.3f} ms per frame", ms);
        check(ms < 4.0);
    });

    return s.run(argc, argv);
}
