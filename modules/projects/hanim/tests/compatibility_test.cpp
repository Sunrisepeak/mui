// Public facade compatibility is intentional; implementation consumers use
// hanim.core. Import both to catch duplicated module ownership/definitions.
import std;
import hanim;
import hanim.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"hanim.compatibility", [] {
    "legacy and core imports expose one public vocabulary and seekable motion"_test = [] {
        static_assert(hanim::Animatable<hanim::Vec2>);
        static_assert(hanim::version_major == 0 && hanim::version_minor == 1 && hanim::version_patch == 0);
        hanim::Motion<hanim::Vec2> movement({0, 0});
        movement.tween_to(hanim::seconds(0), {80, 40}, hanim::seconds(1), hanim::ease::linear);
        expect(movement.value(hanim::seconds(0.5)) == hanim::Vec2{40, 20});
        expect(movement.value(hanim::seconds(1)) == hanim::Vec2{80, 40});
        expect(movement.value(hanim::seconds(0)) == hanim::Vec2{0, 0});
        expect(movement.value(hanim::seconds(0.5)) == hanim::Vec2{40, 20});
        const mlibs::time::Rate fps{60, 1};
        expect(eq(std::bit_cast<std::uint64_t>(double(fps.time_of(60).count())), std::bit_cast<std::uint64_t>(double(hanim::seconds(1).count()))));
    };
    "legacy facade retains renderer-independent prepared geometry without another implementation"_test = [] {
        const std::array edges{hanim::ShapeSegment::line({-1, 1}), hanim::ShapeSegment::line({1, 1})};
        auto plan = hanim::PreparedShapeMorph::prepare({{0, -1}, edges}, {{0, -1}, edges});
        expect(plan.has_value());
        if (!plan) return;
        std::vector<hanim::ShapeCubic> start(plan->segment_count()), end(start.size()), middle(start.size());
        expect(plan->sample(0, start).has_value());
        expect(plan->sample(1, end).has_value());
        expect(plan->sample(0.5, middle).has_value());
        for (std::size_t i = 0; i < start.size(); ++i) {
            expect(start[i].start == end[i].start && start[i].end == end[i].end);
            expect(start[i].start == middle[i].start && start[i].end == middle[i].end);
        }
    };
    "the product import alone supports animation first playback without a renderer"_test = [] {
        auto made=hanim::Engine::make();expect(made.has_value());if(!made)return;
        auto& engine=*made;
        const auto a=engine.create_object({{{.id="item"}}});
        const auto b=engine.create_object({{{.id="item",.local={.tx=40}}}});
        expect(a && b);if(!a || !b)return;
        const auto recipe=hanim::anim::morph(*a,*b).duration(std::chrono::milliseconds(600))
            .with_effect(hanim::fx::translation_path(hanim::PointPath::bend(.25)))
            .with_effect(hanim::fx::blur(0,8));
        const auto playback=engine.play(recipe);expect(playback.has_value());if(!playback)return;
        expect(engine.seek(*playback,hanim::seconds(.3)).has_value());
        const auto frame=engine.frame(*playback);expect(frame.has_value());if(!frame)return;
        expect(frame->geometry.parts[0].local.tx==20 && frame->geometry.parts[0].local.ty==10 && frame->effect_values[0]==4);
        expect(engine.capture_object(*a)->parts()[0].local.tx==0);
        expect(engine.release(*playback).has_value());
    };
}};
