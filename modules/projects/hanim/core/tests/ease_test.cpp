import std;
import hanim.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"hanim.ease", [] {

    "prepared consumers share parameter validation without forbidding overshoot"_test = [] {
        using namespace hanim;
        expect(ease::out_back.valid() && Ease::bezier(.2, -4, .8, 3).valid());
        expect(Ease::stepped(12).valid());
        for (const auto bad : {Ease{static_cast<EaseKind>(255)},
            Ease{EaseKind::linear, {std::numeric_limits<double>::quiet_NaN()}},
            Ease{EaseKind::cubic_bezier, {-1, 0, .5, 1}},
            Ease{EaseKind::steps, {2.5, 1}}, Ease{EaseKind::steps, {4, 4}},
            Ease{EaseKind::steps, {1000001, 1}}}) expect(!bad.valid());
    };

    "every named curve starts at 0 and ends at 1"_test = [] {
        for (int k = 0; k <= static_cast<int>(hanim::EaseKind::in_out_bounce); ++k) {
            hanim::Ease e{static_cast<hanim::EaseKind>(k)};
            expect(eq(std::bit_cast<std::uint64_t>(double(e(0.0))), std::bit_cast<std::uint64_t>(double(0.0))));
            expect(eq(std::bit_cast<std::uint64_t>(double(e(1.0))), std::bit_cast<std::uint64_t>(double(1.0))));
            expect(!std::isnan(e(0.37))) << hanim::to_string(e);
        }
    };

    "power curves are monotonic"_test = [] {
        for (auto e : {hanim::ease::in_quad, hanim::ease::out_cubic, hanim::ease::in_out_quint, hanim::ease::out_sine,
                       hanim::ease::in_out_expo, hanim::ease::out_circ, hanim::ease::css_ease}) {
            double prev = -1;
            for (int i = 0; i <= 1000; ++i) {
                double v = e(i / 1000.0);
                expect(v >= prev) << hanim::to_string(e);
                prev = v;
            }
        }
    };

    "cubic-bezier matches the CSS reference values"_test = [] {
        // Reference values from a 200-step bisection in double precision.
        expect(near(hanim::ease::css_ease(0.5), 0.802403387584857, 1e-12));
        expect(near(hanim::ease::css_ease_in_out(0.5), 0.5, 1e-12));
        expect(near(hanim::ease::css_ease_in(0.5), 0.31535681257253945, 1e-12));
        // A diagonal bezier is linear.
        expect(eq(std::bit_cast<std::uint64_t>(double(hanim::Ease::bezier(0.3, 0.3, 0.7, 0.7)(0.42))), std::bit_cast<std::uint64_t>(double(0.42))));
        // Overshooting y is allowed.
        expect(hanim::Ease::bezier(0.5, 1.8, 0.5, 1.0)(0.5) > 1.0);
    };

    "steps follow CSS jump positions"_test = [] {
        auto end = hanim::Ease::stepped(4, hanim::StepPosition::jump_end);
        expect(eq(std::bit_cast<std::uint64_t>(double(end(0.0))), std::bit_cast<std::uint64_t>(double(0.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(end(0.24))), std::bit_cast<std::uint64_t>(double(0.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(end(0.25))), std::bit_cast<std::uint64_t>(double(0.25))));
        expect(eq(std::bit_cast<std::uint64_t>(double(end(0.99))), std::bit_cast<std::uint64_t>(double(0.75))));
        expect(eq(std::bit_cast<std::uint64_t>(double(end(1.0))), std::bit_cast<std::uint64_t>(double(1.0))));
        auto start = hanim::Ease::stepped(4, hanim::StepPosition::jump_start);
        expect(eq(std::bit_cast<std::uint64_t>(double(start(0.0))), std::bit_cast<std::uint64_t>(double(0.25))));
        expect(eq(std::bit_cast<std::uint64_t>(double(start(0.8))), std::bit_cast<std::uint64_t>(double(1.0))));
        auto none = hanim::Ease::stepped(5, hanim::StepPosition::jump_none);
        expect(eq(std::bit_cast<std::uint64_t>(double(none(0.0))), std::bit_cast<std::uint64_t>(double(0.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(none(0.5))), std::bit_cast<std::uint64_t>(double(0.5))));
        expect(eq(std::bit_cast<std::uint64_t>(double(none(1.0))), std::bit_cast<std::uint64_t>(double(1.0))));
        auto both = hanim::Ease::stepped(3, hanim::StepPosition::jump_both);
        expect(eq(std::bit_cast<std::uint64_t>(double(both(0.0))), std::bit_cast<std::uint64_t>(double(0.25))));
        expect(eq(std::bit_cast<std::uint64_t>(double(both(1.0))), std::bit_cast<std::uint64_t>(double(1.0))));
    };

    "back and elastic overshoot"_test = [] {
        expect(hanim::ease::out_back(0.6) > 1.0);
        expect(hanim::ease::in_back(0.2) < 0.0);
        bool above = false;
        for (int i = 1; i < 100; ++i) above = above || hanim::ease::out_elastic(i / 100.0) > 1.0;
        expect(above);
    };

    "text form round-trips"_test = [] {
        for (int k = 0; k <= static_cast<int>(hanim::EaseKind::in_out_bounce); ++k) {
            hanim::Ease e{static_cast<hanim::EaseKind>(k)};
            auto back = hanim::parse_ease(hanim::to_string(e));
            expect(back.has_value() && *back == e) << hanim::to_string(e);
        }
        auto b = hanim::parse_ease("cubic-bezier(0.2, 0, 0, 1)");
        expect(b.has_value() && *b == hanim::ease::standard);
        expect(hanim::parse_ease(hanim::to_string(hanim::ease::standard)) == hanim::ease::standard);
        expect(hanim::parse_ease("ease-in-out") == hanim::ease::css_ease_in_out);
        auto st = hanim::parse_ease("steps(4, jump-start)");
        expect(st.has_value() && *st == hanim::Ease::stepped(4, hanim::StepPosition::jump_start));
        expect(hanim::parse_ease("steps(3, end)") == hanim::Ease::stepped(3, hanim::StepPosition::jump_end));
        expect(!hanim::parse_ease("cubic-bezier(1.5, 0, 0, 1)").has_value()) << "x outside [0, 1] is refused";
        expect(!hanim::parse_ease("bouncy").has_value());
        expect(!hanim::parse_ease("steps(0)").has_value());
    };

    "a spring is a curve: its unit response, at its own pace, exactly 1 once settled"_test = [] {
        using namespace hanim;
        for (const SpringParams params : {springs::standard, springs::wobbly, springs::snappy, springs::molasses}) {
            const Ease e = spring_ease(params);
            expect(e.valid() && e.settle() > 0) << to_string(e);
            const Oscillator o(0, 0, 1, params, {1e-4, 1e-3});
            expect(eq(e.settle(), o.settle_time()));
            expect(eq(e(0.0), 0.0));
            expect(eq(e(1.0), 1.0));
            for (const double t : {.1, .25, .5, .9}) expect(eq(e(t), o.position(t * e.settle())));
            expect(parse_ease(to_string(e)) == e) << to_string(e);
        }
        bool over = false;
        for (int i = 1; i < 100; ++i) over = over || spring_ease(springs::wobbly)(i / 100.0) > 1.0;
        expect(over) << "a wobbly spring overshoots";
        expect(!Ease::spring(0, 26).valid() && !Ease::spring(170, -1).valid() && !parse_ease("spring(170)").has_value()) << "a spring needs stiffness, damping";
        expect(parse_ease("spring(170, 26)") == spring_ease(springs::standard)) << "mass 1 by default";
    };

    "slope approximates the derivative"_test = [] {
        expect(near(hanim::ease::linear.slope(0.5), 1.0, 1e-12));
        expect(near(hanim::ease::in_quad.slope(0.5), 1.0, 1e-6));
        expect(near(hanim::ease::out_cubic.slope(0.0), 3.0, 2e-3));
    };

    "smoothstep and a window's eased progress: exact ends, flat there, the specification's formula"_test = [] {
        const auto e = hanim::ease::smoothstep;
        expect(e.valid() && e(0) == 0 && e(1) == 1 && e(.5) == .5) << "0, ½, 1";
        for (const double t : {.1, .3, .7, .9}) expect(e(t) == t * t * (3 - 2 * t)) << "t²(3 − 2t)";
        expect(e.slope(0) < 1e-3 && e.slope(1) < 1e-3) << "flat at both ends";
        expect(hanim::to_string(e) == "smoothstep" && hanim::parse_ease("smoothstep") == e) << "named";
        expect(hanim::window(1, 2, 4) == 0 && hanim::window(3, 2, 4) == .5 && hanim::window(5, 2, 4) == 1) << "before, within, after";
        expect(hanim::window(3, 4, 2) == .5 && hanim::window(4.5, 4, 2) == 0) << "a window backwards";
        expect(hanim::window(2, 2, 2) == 1 && hanim::window(1.9, 2, 2) == 0) << "an empty window: a step at its moment";
        expect(hanim::window(2.5, 2, 4, e) == e(.25)) << "eased";
    };
}};
