import std;
import hanim;
import openmui.testing;

using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"hanim.ease"};

    s.test("every named curve starts at 0 and ends at 1", [] {
        for (int k = 0; k <= static_cast<int>(hanim::EaseKind::in_out_bounce); ++k) {
            hanim::Ease e{static_cast<hanim::EaseKind>(k)};
            check_bits(e(0.0), 0.0);
            check_bits(e(1.0), 1.0);
            check(!std::isnan(e(0.37)), hanim::to_string(e));
        }
    });

    s.test("power curves are monotonic", [] {
        for (auto e : {hanim::ease::in_quad, hanim::ease::out_cubic, hanim::ease::in_out_quint, hanim::ease::out_sine,
                       hanim::ease::in_out_expo, hanim::ease::out_circ, hanim::ease::css_ease}) {
            double prev = -1;
            for (int i = 0; i <= 1000; ++i) {
                double v = e(i / 1000.0);
                check(v >= prev, hanim::to_string(e));
                prev = v;
            }
        }
    });

    s.test("cubic-bezier matches the CSS reference values", [] {
        // Reference values from a 200-step bisection in double precision.
        check_near(hanim::ease::css_ease(0.5), 0.802403387584857, 1e-12);
        check_near(hanim::ease::css_ease_in_out(0.5), 0.5, 1e-12);
        check_near(hanim::ease::css_ease_in(0.5), 0.31535681257253945, 1e-12);
        // A diagonal bezier is linear.
        check_bits(hanim::Ease::bezier(0.3, 0.3, 0.7, 0.7)(0.42), 0.42);
        // Overshooting y is allowed.
        check(hanim::Ease::bezier(0.5, 1.8, 0.5, 1.0)(0.5) > 1.0);
    });

    s.test("steps follow CSS jump positions", [] {
        auto end = hanim::Ease::stepped(4, hanim::StepPosition::jump_end);
        check_bits(end(0.0), 0.0);
        check_bits(end(0.24), 0.0);
        check_bits(end(0.25), 0.25);
        check_bits(end(0.99), 0.75);
        check_bits(end(1.0), 1.0);
        auto start = hanim::Ease::stepped(4, hanim::StepPosition::jump_start);
        check_bits(start(0.0), 0.25);
        check_bits(start(0.8), 1.0);
        auto none = hanim::Ease::stepped(5, hanim::StepPosition::jump_none);
        check_bits(none(0.0), 0.0);
        check_bits(none(0.5), 0.5);
        check_bits(none(1.0), 1.0);
        auto both = hanim::Ease::stepped(3, hanim::StepPosition::jump_both);
        check_bits(both(0.0), 0.25);
        check_bits(both(1.0), 1.0);
    });

    s.test("back and elastic overshoot", [] {
        check(hanim::ease::out_back(0.6) > 1.0);
        check(hanim::ease::in_back(0.2) < 0.0);
        bool above = false;
        for (int i = 1; i < 100; ++i) above = above || hanim::ease::out_elastic(i / 100.0) > 1.0;
        check(above);
    });

    s.test("text form round-trips", [] {
        for (int k = 0; k <= static_cast<int>(hanim::EaseKind::in_out_bounce); ++k) {
            hanim::Ease e{static_cast<hanim::EaseKind>(k)};
            auto back = hanim::parse_ease(hanim::to_string(e));
            check(back.has_value() && *back == e, hanim::to_string(e));
        }
        auto b = hanim::parse_ease("cubic-bezier(0.2, 0, 0, 1)");
        check(b.has_value() && *b == hanim::ease::standard);
        check(hanim::parse_ease(hanim::to_string(hanim::ease::standard)) == hanim::ease::standard);
        check(hanim::parse_ease("ease-in-out") == hanim::ease::css_ease_in_out);
        auto st = hanim::parse_ease("steps(4, jump-start)");
        check(st.has_value() && *st == hanim::Ease::stepped(4, hanim::StepPosition::jump_start));
        check(hanim::parse_ease("steps(3, end)") == hanim::Ease::stepped(3, hanim::StepPosition::jump_end));
        check(!hanim::parse_ease("cubic-bezier(1.5, 0, 0, 1)").has_value(), "x outside [0, 1] is refused");
        check(!hanim::parse_ease("bouncy").has_value());
        check(!hanim::parse_ease("steps(0)").has_value());
    });

    s.test("slope approximates the derivative", [] {
        check_near(hanim::ease::linear.slope(0.5), 1.0, 1e-12);
        check_near(hanim::ease::in_quad.slope(0.5), 1.0, 1e-6);
        check_near(hanim::ease::out_cubic.slope(0.0), 3.0, 2e-3);
    });

    return s.run(argc, argv);
}
