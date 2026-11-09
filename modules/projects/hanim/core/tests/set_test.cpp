import std;
import hanim.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using hanim::seconds;

UTest cases{"hanim.set", [] {

    "evaluate agrees with per-channel values"_test = [] {
        hanim::Set set;
        auto a = set.tween(0, 10, seconds(0), seconds(1), hanim::ease::linear);
        auto b = set.spring(0, 100, seconds(0), hanim::springs::wobbly);
        auto c = set.decay(0, 500, seconds(0));
        auto d = set.hold(7);
        std::vector<double> out(set.capacity());
        for (double t = 0; t < 3; t += 0.1) {
            auto moving = hanim::evaluate(set, seconds(t), out);
            expect(eq(std::bit_cast<std::uint64_t>(double(out[a.index])), std::bit_cast<std::uint64_t>(double(set.value(a, seconds(t))))));
            expect(eq(std::bit_cast<std::uint64_t>(double(out[b.index])), std::bit_cast<std::uint64_t>(double(set.value(b, seconds(t))))));
            expect(eq(std::bit_cast<std::uint64_t>(double(out[c.index])), std::bit_cast<std::uint64_t>(double(set.value(c, seconds(t))))));
            expect(eq(std::bit_cast<std::uint64_t>(double(out[d.index])), std::bit_cast<std::uint64_t>(double(7.0))));
            expect(moving <= 3);
        }
        expect(hanim::evaluate(set, seconds(100), out) == 0) << "all at rest eventually";
    };

    "retargeting a spring keeps position and velocity"_test = [] {
        hanim::Set set;
        auto ch = set.spring(0, 100, seconds(0), hanim::springs::gentle);
        double t = 0.2;
        double x = set.value(ch, seconds(t));
        double v = set.velocity(ch, seconds(t));
        set.spring_to(ch, seconds(t), -20);
        expect(eq(std::bit_cast<std::uint64_t>(double(set.value(ch, seconds(t)))), std::bit_cast<std::uint64_t>(double(x))));
        expect(std::abs(set.velocity(ch, seconds(t)) - v) < 1e-9);
        expect(eq(std::bit_cast<std::uint64_t>(double(set.value(ch, set.settle_time(ch)))), std::bit_cast<std::uint64_t>(double(-20.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(set.target(ch))), std::bit_cast<std::uint64_t>(double(-20.0))));
    };

    "seek consistency: 1000 random frames, seeked after the fact, are the frames played (M2 ③)"_test = [] {
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
                    expect(std::abs(set.velocity(ch, seconds(st.t)) - before) < 1e-9) << std::format("velocity continuous at {}", st.t);
            }
            played.push_back(set.value(ch, seconds(t)));
        }
        // Seeked: the finished channel asked about any time — its segments are its history.
        std::uint64_t state = 20261027;
        auto next_random = [&] {
            state += mlibs::math::golden;
            return mlibs::math::finalize(state);
        };
        int differing = 0;
        for (int i = 0; i < 1000; ++i) {
            const auto f = static_cast<int>(next_random() % (frames + 1));
            if (std::bit_cast<std::uint64_t>(set.value(ch, seconds(f / 60.0))) != std::bit_cast<std::uint64_t>(played[static_cast<std::size_t>(f)]))
                ++differing;
        }
        expect(eq(differing, 0));
    };

    "reduced motion shows targets"_test = [] {
        hanim::Set set;
        auto ch = set.tween(0, 1, seconds(0), seconds(1));
        expect(eq(std::bit_cast<std::uint64_t>(double(set.value(ch, seconds(0.1), {.reduce_motion = true}))), std::bit_cast<std::uint64_t>(double(1.0))));
    };

    "removed channels are invalid and their slots reused"_test = [] {
        hanim::Set set;
        auto a = set.hold(1);
        set.remove(a);
        expect(!set.valid(a));
        auto b = set.hold(2);
        expect(b.index == a.index && b.generation != a.generation);
        expect(set.size() == 1);
        set.clear();
        expect(set.size() == 0 && !set.valid(b));
    };

    "evaluating does not allocate"_test = [] {
        hanim::Set set;
        for (int i = 0; i < 1000; ++i) (void)set.spring(0, i, seconds(i * 0.001), hanim::springs::standard);
        std::vector<double> out(set.capacity());
        // A timing smoke test rather than an allocation count (no allocator
        // hook in a std-only package): 1000 springs in well under a frame.
        auto start = std::chrono::steady_clock::now();
        for (int f = 0; f < 60; ++f) (void)hanim::evaluate(set, seconds(f / 60.0), out);
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 60;
        std::println("    1000 springs: {:.3f} ms per frame", ms);
        expect(ms < 4.0);
    };
}};
