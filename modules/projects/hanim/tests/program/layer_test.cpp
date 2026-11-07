// Layers on one channel (convergence plan §6.8 ⑥): writes that add,
// multiply or mix over the channel's base instead of rivalling it — a breath
// over a drag — combined in author order; a second replacing writer is
// still refused.
import std;
import hanim.engine;
import mlibs.testing;

using namespace hanim;
using namespace mlibs::testing;

namespace {
inline constexpr auto glow = ch::define<double>("app.glow");

ObjectDescription shape() {
    const auto contour = Contour::copy(
        {{0, -1},
         std::vector<ShapeSegment>{ShapeSegment::line({-1, 1}), ShapeSegment::line({1, 1})},
         true});
    return {{{.id = "shape", .contours = {*contour}, .fill = Rgba{1, 0, 0, 1}}}};
}
struct Fixture {
    Engine engine = *Engine::make();
    ObjectRef object = *engine.create_object(shape());
    std::expected<PreparedProgram, EngineFailure> prepare(const Program& program) {
        return engine.prepare(program);
    }
};
struct Pose {
    Transform2D local;
    std::optional<double> glow;
};
Pose at(const PreparedProgram& program, double time) {
    const auto& needed = program.tracks()[0].requirements;
    std::vector<FramePart> parts(needed.parts);
    std::vector<ShapeCubic> cubics(needed.cubics);
    std::vector<double> values(needed.values);
    std::vector<float> effects(program.tracks()[0].max_effect_parameters);
    const auto sampled = program.sample(0, seconds(time), {parts, cubics, values}, effects);
    check(sampled.has_value(), sampled ? "" : sampled.error().text());
    if (!sampled)
        return {};
    return {sampled->geometry.parts[0].local, sampled->geometry.value(0, glow)};
}
bool near(double left, double right) {
    return std::abs(left - right) < 1e-9;
}
}  // namespace

int main(int argc, char** argv) {
    Suite suite{"hanim.layer"};

    suite.test("an added layer rides on the base: a sway over a move", [] {
        Fixture f;
        const auto move =
            anim::to(f.object, PropertyTargets{.position = Vec2{100, 0}}).duration(seconds(1));
        const auto sway = anim::to(f.object, PropertyTargets{.position = Vec2{0, 10}})
                              .duration(seconds(1))
                              .blend(Blend::add);
        const auto program = f.prepare(parallel(move, sway));
        check(program.has_value(), program ? "" : program.error().text());
        if (!program)
            return;
        const auto half = at(*program, .5), end = at(*program, 1);
        check(near(half.local.tx, 50) && near(half.local.ty, 5),
              std::format("halfway ({}, {})", half.local.tx, half.local.ty));
        check(near(end.local.tx, 100) && near(end.local.ty, 10));
        const auto rival =
            f.prepare(parallel(move, anim::to(f.object, PropertyTargets{.position = Vec2{0, 10}})
                                         .duration(seconds(1))));
        check(!rival && rival.error().caused_by(ProgramError::binding_conflict),
              "two replacing writers still clash");
    });

    suite.test("multiply scales the base; mix moves it towards the layer by its weight", [] {
        Fixture f;
        const auto grow =
            anim::to(f.object, PropertyTargets{.scale = Vec2{2, 2}}).duration(seconds(1));
        const auto boost = anim::to(f.object, PropertyTargets{.scale = Vec2{1.5, 1.5}})
                               .duration(seconds(1))
                               .blend(Blend::multiply);
        const auto scaled = f.prepare(parallel(grow, boost));
        check(scaled && near(at(*scaled, 1).local.sx, 3), "2 × 1.5");
        const auto pulled = f.prepare(anim::to(f.object, PropertyTargets{.position = Vec2{100, 0}})
                                          .duration(seconds(1))
                                          .blend(Blend::mix, .5));
        check(pulled && near(at(*pulled, 1).local.tx, 50) && near(at(*pulled, .5).local.tx, 25),
              "halfway towards, half weighted");
    });

    suite.test("layers combine in author order, over values too", [] {
        Fixture f;
        const auto add = anim::to(f.object, PropertyTargets{.scale = Vec2{.5, .5}})
                             .duration(seconds(1))
                             .blend(Blend::add);
        const auto twice = anim::to(f.object, PropertyTargets{.scale = Vec2{2, 2}})
                               .duration(seconds(1))
                               .blend(Blend::multiply);
        const auto first_add = f.prepare(parallel(add, twice));
        const auto first_twice = f.prepare(parallel(twice, add));
        check(first_add && near(at(*first_add, 1).local.sx, 3), "(1 + 0.5) × 2");
        check(first_twice && near(at(*first_twice, 1).local.sx, 2.5), "1 × 2 + 0.5");
        const auto base = anim::to(f.object, glow = 1.0).duration(seconds(1));
        const auto extra = anim::to(f.object, glow = .5).duration(seconds(1)).blend(Blend::add);
        const auto glowing = f.prepare(parallel(base, extra));
        check(glowing && at(*glowing, 1).glow == std::optional<double>(1.5),
              "a value channel's layer adds too");
    });

    suite.test("a layer must make sense for its channel and weigh a finite amount", [] {
        Fixture f;
        check(!f.prepare(
                  anim::to(f.object, PropertyTargets{.fill = Rgba{0, 0, 1, 1}}).blend(Blend::add)),
              "colours only mix");
        check(!f.prepare(anim::to(f.object, PropertyTargets{.position = Vec2{1, 1}})
                             .blend(Blend::multiply)),
              "positions add, not multiply");
        check(!f.prepare(anim::to(f.object, PropertyTargets{.position = Vec2{1, 1}})
                             .blend(Blend::add, std::numeric_limits<double>::infinity())));
        check(f.prepare(anim::to(f.object, PropertyTargets{.fill = Rgba{0, 0, 1, 1}})
                            .blend(Blend::mix, .5))
                  .has_value(),
              "a colour mixes");
    });

    return suite.run(argc, argv);
}
