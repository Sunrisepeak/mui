// Public facade compatibility is intentional; implementation consumers use
// hanim.core. Import both to catch duplicated module ownership/definitions.
import std;
import hanim;
import hanim.core;
import openmui.testing;

using namespace omui::testing;

int main(int argc, char** argv) {
    Suite suite{"hanim.compatibility"};
    suite.test("legacy and core imports expose one public vocabulary and seekable motion", [] {
        static_assert(hanim::Animatable<hanim::Vec2>);
        static_assert(hanim::version_major == 0 && hanim::version_minor == 1 && hanim::version_patch == 0);
        hanim::Motion<hanim::Vec2> movement({0, 0});
        movement.tween_to(hanim::seconds(0), {80, 40}, hanim::seconds(1), hanim::ease::linear);
        check(movement.value(hanim::seconds(0.5)) == hanim::Vec2{40, 20});
        check(movement.value(hanim::seconds(1)) == hanim::Vec2{80, 40});
        check(movement.value(hanim::seconds(0)) == hanim::Vec2{0, 0});
        check(movement.value(hanim::seconds(0.5)) == hanim::Vec2{40, 20});
        const hanim::FrameRate fps{60, 1};
        check_bits(fps.time_of(60).count(), hanim::seconds(1).count());
    });
    suite.test("legacy facade retains renderer-independent prepared geometry without another implementation", [] {
        const std::array edges{hanim::ShapeSegment::line({-1, 1}), hanim::ShapeSegment::line({1, 1})};
        auto plan = hanim::PreparedShapeMorph::prepare({{0, -1}, edges}, {{0, -1}, edges});
        check(plan.has_value());
        if (!plan) return;
        std::vector<hanim::ShapeCubic> start(plan->segment_count()), end(start.size()), middle(start.size());
        check(plan->sample(0, start).has_value());
        check(plan->sample(1, end).has_value());
        check(plan->sample(0.5, middle).has_value());
        for (std::size_t i = 0; i < start.size(); ++i) {
            check(start[i].start == end[i].start && start[i].end == end[i].end);
            check(start[i].start == middle[i].start && start[i].end == middle[i].end);
        }
    });
    return suite.run(argc, argv);
}
