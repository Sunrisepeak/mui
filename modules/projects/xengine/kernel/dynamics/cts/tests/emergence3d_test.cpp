// The emergence set in 3D (the foundation plan's §9.3; the M6.5 design's T5.7): each case a scene of bodies,
// particles and the air, and an assertion on what became of them — none of it by a rule naming a material. The
// set's 9, 10 and 11 are in objects_test (an updraft lifts a sheet, grass burns faster downwind, lightning takes
// the iron pole), its 1 in xengine.particles (sand heaps, water runs flat, oil floats).
import std;
import xengine.dynamics.scene;
import xengine.matter.json;
import mlibs.geom;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace rigid3 = xengine::dynamics::rigid3;
namespace matter = xengine::matter;
namespace particles = xengine::particles;
namespace field = xengine::field;
namespace math = mlibs::math;
namespace geom = mlibs::geom;

namespace {

std::shared_ptr<const matter::Program> standard() {
    static const std::shared_ptr<const matter::Program> program = [] {
        auto made = matter::standard();
        expect(fatal(made.has_value()));
        return std::make_shared<const matter::Program>(std::move(*made));
    }();
    return program;
}

std::uint32_t stored(const matter::Program& program, std::uint32_t channel, double value) {
    return static_cast<std::uint32_t>(std::lround(std::ldexp(value, static_cast<int>(program.channels()[channel].fraction))));
}

}  // namespace

UTest cases{"dynamics.emergence3d", [] {
    "3: a bucket of water on a burning crate puts it out — wet wood is harder to light; a few drops on it boil off as steam, which rises, cools and comes back as water; the crate left dry burns on"_test = [] {
        const auto program = standard();
        const std::uint32_t temperature = *program->channel("temperature"), burning = *program->channel("burning"), wetness = *program->channel("wetness");
        const matter::MaterialId water = *program->material("water"), steam = *program->material("steam");
        // A crate alight, and from 4 s water poured over its top: a drop every `every` steps for `steps` steps.
        struct Out {
            std::uint32_t burning_at_ten = 0, wetness = 0;  // burning 10 s in (a crate this size burns out at about 16 s)
            int           out_at = 0;
            std::size_t   boiled = 0, condensed = 0;
            float         steam_high = 0;
        };
        const auto play = [&](int every, int steps) {
            auto world = rigid3::World::make({});
            expect(fatal(world.has_value()));
            (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
            const rigid3::BodyId crate = *world->add({.at = {0, 0.5f, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), {.density = 600});
            auto objects = rigid3::Objects::make(*program, {.ambient = *program->material("air")});
            expect(fatal(objects.has_value()));
            (void)objects->add(crate, *program->material("wood"));
            auto air = field::Field::make(*program, {.air = *program->material("air")});
            expect(fatal(air.has_value()));
            auto cloud = particles::Cloud::make(program, {.ambient = *program->material("air"), .touching = true});
            expect(fatal(cloud.has_value()));
            // What the particles meet: the ground (no body) and the crate's faces.
            const geom::Mesh ground = geom::sheet(20, 20, 2, 2), box = geom::box({0.5f, 0.5f, 0.5f});
            const geom::Bvh ground_tree = geom::bvh_of(ground), box_tree = geom::bvh_of(box);
            const std::array<rigid3::BodyId, 2> surface_bodies{rigid3::BodyId{}, crate};
            rigid3::Scene scene;
            Out out;
            for (int step = 1; step <= 1200; ++step) {
                world->step();
                if (step < 60) (void)objects->set(crate, temperature, std::max(*objects->get(crate, temperature), stored(*program, temperature, 900)));  // a torch to it
                if (step >= 240 && step < 240 + steps && (step - 240) % every == 0) {
                    const float turn = static_cast<float>(step) * 2.39996f, out_from = 0.02f + 0.1f * static_cast<float>(step % 7) / 7;
                    const std::array<particles::Spawn, 1> drop{{{.at = {out_from * static_cast<float>(math::det::cos(turn)), 1.4f, out_from * static_cast<float>(math::det::sin(turn))}, .velocity = {0, -2, 0}, .life = 60, .material = water}}};
                    cloud->emit(drop);
                }
                const auto pose = world->pose(crate);
                const std::array<particles::Surface, 2> surfaces{{{.mesh = &ground, .tree = &ground_tree, .place = math::translation({0, 0, 0})},
                                                                   {.mesh = &box, .tree = &box_tree, .place = math::translation(pose->centre)}}};
                cloud->fly(surfaces, scene.winds(*air, *cloud));
                const auto stepped = scene.step(*world, *objects, *cloud, surface_bodies, *air);
                expect(fatal(stepped.has_value())) << (stepped ? "" : stepped.error().message);
                for (const matter::Event& event : cloud->events())
                    if (event.kind == matter::EventKind::became) out.boiled += event.from == water && event.to == steam, out.condensed += event.from == steam && event.to == water;
                for (std::size_t particle = 0; particle < cloud->size(); ++particle)
                    if (cloud->material(particle) == steam) out.steam_high = std::max(out.steam_high, cloud->at()[particle].y);
                if (step > 240 && !out.out_at && *objects->get(crate, burning) == 0) out.out_at = step;
                if (step == 600) out.burning_at_ten = *objects->get(crate, burning);
            }
            out.wetness = *objects->get(crate, wetness);
            return out;
        };
        // A bucket: 120 drops in two seconds (each drop a unit of water, as a lattice's cell of it is). A few drops:
        // one every half second for four seconds. None: the crate left to burn.
        const Out bucket = play(1, 120), few = play(30, 240), none = play(1, 0);
        std::println("a bucket on the burning crate: out at step {} ({:.1f} s after the water came), wetness {}; a few drops: {} boiled off, the steam as high as {:.2f} m, {} came back as water, at 10 s the crate burning {}; left alone: burning {}",
                     bucket.out_at, (bucket.out_at - 240) / 60.0, bucket.wetness, few.boiled, few.steam_high, few.condensed, few.burning_at_ten, none.burning_at_ten);
        expect(gt(bucket.out_at, 0) && eq(bucket.burning_at_ten, 0u) && gt(bucket.wetness, 0u)) << "the bucket put it out, wetting it";
        expect(gt(few.boiled, std::size_t{0}) && gt(few.steam_high, 1.5f)) << "drops on the burning wood boiled off, and the steam rose";
        expect(gt(few.condensed, std::size_t{0})) << "the steam, cooled in the air, came back as water";
        expect(gt(few.burning_at_ten, 0u) && gt(none.burning_at_ten, 0u)) << "a few drops do not put it out; nor does nothing";
    };
}};
