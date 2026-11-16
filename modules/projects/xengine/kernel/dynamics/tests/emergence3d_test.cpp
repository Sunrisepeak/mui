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

// Particles in a tank, taking room: a box of `half` about `centre`, its inside the walls.
struct Tank {
    geom::Mesh                         mesh;
    geom::Bvh                          tree;
    std::array<particles::Surface, 1>  inside;
    Tank(math::Vec3 half, math::Vec3 centre) : mesh(geom::box(half)), tree(geom::bvh_of(mesh)) { inside = {{{.mesh = &mesh, .tree = &tree, .place = math::translation(centre)}}}; }
};

// Layers of one material in a tank's floor, 2 cm apart and off a perfect lattice by a little (the same each run).
void layers(std::vector<particles::Spawn>& out, matter::MaterialId material, int from, int count, float half, std::optional<float> kelvin = std::nullopt) {
    const int across = static_cast<int>(half * 2 / 0.02f) - 1;
    for (int layer = from; layer < from + count; ++layer)
        for (int row = 0; row < across; ++row)
            for (int column = 0; column < across; ++column)
                out.push_back({.at = {-half + 0.02f + 0.02f * static_cast<float>(column) + 0.002f * static_cast<float>((layer * 7 + row * 3) % 5 - 2), 0.012f + 0.021f * static_cast<float>(layer),
                                      -half + 0.02f + 0.02f * static_cast<float>(row) + 0.002f * static_cast<float>((layer * 5 + column * 3) % 5 - 2)},
                               .life = 1000, .material = material, .kelvin = kelvin});
}

// An explanation, said: each link's who, step, law and what came in from whom.
std::string said(const matter::Program& program, const std::vector<matter::Explained>& chain) {
    const auto law = [&](std::uint32_t index) -> std::string_view { return index < program.laws().size() ? std::string_view(program.laws()[index].name) : std::string_view("—"); };
    const auto who = [](matter::Who one) { return one.carrier == rigid3::bodies_carrier ? std::format("body {}", one.id) : one.carrier == particles::particles_carrier ? std::format("particle {}", one.id) : std::string("the air"); };
    std::string out;
    for (const matter::Explained& link : chain)
        out += std::format(" {} at {}: {}{};", who(link.who), link.cause.step, link.cause.kind == matter::CauseKind::applied ? std::string_view("applied") : law(link.cause.law),
                           link.cause.from ? std::format(" (in by `{}` from {})", law(link.cause.inflow), who(link.cause.from)) : link.cause.through ? std::string(" (through the air)") : std::string());
    return out;
}
// Whether a link of the chain was made by `name`, and (if given) came from `from`.
bool names(const matter::Program& program, const std::vector<matter::Explained>& chain, std::string_view name, std::optional<matter::Who> from = std::nullopt) {
    return std::ranges::any_of(chain, [&](const matter::Explained& link) {
        return link.cause.law < program.laws().size() && program.laws()[link.cause.law].name == name && (!from || link.cause.from == *from);
    });
}

std::size_t count_of(const particles::Cloud& cloud, matter::MaterialId material) {
    std::size_t out = 0;
    for (std::size_t particle = 0; particle < cloud.size(); ++particle) out += cloud.material(particle) == material;
    return out;
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
            std::vector<matter::Explained> drop;  // why the first drop that boiled did
            bool          drop_from_crate = false, crate_burns = false;  // crate_burns: the chain ends at the torch put to the crate
            std::string   drop_said;
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
            // What the particles meet is every body by its own shape, where it is (Scene::surfaces): the ground, met
            // and carrying no matter, and the crate's faces.
            rigid3::Scene scene;
            Out out;
            for (int step = 1; step <= 1200; ++step) {
                if (step < 60) (void)objects->set(crate, temperature, std::max(*objects->get(crate, temperature), stored(*program, temperature, 900)));  // a torch to it
                if (step >= 240 && step < 240 + steps && (step - 240) % every == 0) {
                    const float turn = static_cast<float>(step) * 2.39996f, out_from = 0.02f + 0.1f * static_cast<float>(step % 7) / 7;
                    const std::array<particles::Spawn, 1> drop{{{.at = {out_from * static_cast<float>(math::det::cos(turn)), 1.4f, out_from * static_cast<float>(math::det::sin(turn))}, .velocity = {0, -2, 0}, .life = 60, .material = water}}};
                    cloud->emit(drop);
                }
                const auto stepped = scene.advance(*world, *objects, *cloud, *air);  // pushes, the world, the drops against the bodies, matter
                expect(fatal(stepped.has_value())) << (stepped ? "" : stepped.error().message);
                for (const matter::Event& event : cloud->events()) {
                    if (event.kind != matter::EventKind::became) continue;
                    out.boiled += event.from == water && event.to == steam, out.condensed += event.from == steam && event.to == water;
                    if (event.from == water && event.to == steam && out.drop.empty()) {
                        // The agent's question: why did this drop boil? Back through the crate it lay on.
                        out.drop = rigid3::Scene::explain(particles::who(cloud->ids()[event.instance]), *objects, *cloud);
                        out.drop_from_crate = names(*program, out.drop, "boils") && std::ranges::any_of(out.drop, [&](const matter::Explained& link) { return link.cause.from == rigid3::who(crate); });
                        out.crate_burns = !out.drop.empty() && out.drop.back().who == rigid3::who(crate) && out.drop.back().cause.kind == matter::CauseKind::applied;
                        out.drop_said = said(*program, out.drop);
                    }
                }
                for (std::size_t particle = 0; particle < cloud->size(); ++particle)
                    if (cloud->material(particle) == steam) out.steam_high = std::max(out.steam_high, cloud->at()[particle].y);
                if (step > 240 && !out.out_at && *objects->get(crate, burning) == 0) out.out_at = step;
                if (step == 600) out.burning_at_ten = *objects->get(crate, burning);
            }
            // Every body is a surface, by its own shape; a copy of the scene (a branch's) shares the meshes made.
            rigid3::Scene copy = scene;
            const std::span<const particles::Surface> mine = scene.surfaces(*world);
            expect(eq(mine.size(), world->bodies().size()) && eq(mine[1].mesh->triangles(), std::size_t{12})) << "the ground and the crate, a box's 12 triangles";
            expect(copy.surfaces(*world)[1].mesh == mine[1].mesh) << "shared, not made again";
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
        std::println("   why the first drop boiled:{}", few.drop_said);
        expect(few.drop_from_crate && few.crate_burns) << "explained: it boiled by `boils` with heat from the crate, back to the torch put to the crate" << few.drop_said;
        expect(gt(few.burning_at_ten, 0u) && gt(none.burning_at_ten, 0u)) << "a few drops do not put it out; nor does nothing";
    };

    "4: lava let into water: it sets as stone where the water takes its heat, and the water boils off it as steam"_test = [] {
        const auto program = standard();
        const matter::MaterialId water = *program->material("water"), lava = *program->material("lava"), stone = *program->material("stone"), steam = *program->material("steam");
        Tank tank({0.1f, 0.3f, 0.1f}, {0, 0.3f, 0});
        auto cloud = particles::Cloud::make(program, {.ambient = *program->material("air"), .touching = true});
        expect(fatal(cloud.has_value()));
        std::vector<particles::Spawn> spawns;
        // A thin pool, and three times as much lava let onto it (a drop of lava has less heat above its setting than a
        // drop of water takes to boil: it takes more lava than water to boil any off — as it does in the world).
        layers(spawns, water, 0, 1, 0.1f);
        for (int layer = 0; layer < 3; ++layer)
            for (int drop = 0; drop < 81; ++drop)
                spawns.push_back({.at = {-0.08f + 0.02f * static_cast<float>(drop % 9), 0.1f + 0.021f * static_cast<float>(layer), -0.08f + 0.02f * static_cast<float>(drop / 9)}, .life = 1000, .material = lava});
        cloud->emit(spawns);
        std::size_t set = 0, boiled = 0;
        std::vector<matter::Explained> why_boiled;
        for (int step = 0; step < 600; ++step) {
            expect(cloud->step(tank.inside).has_value());
            for (const matter::Event& event : cloud->events())
                if (event.kind == matter::EventKind::became) {
                    set += event.from == lava && event.to == stone, boiled += event.from == water && event.to == steam;
                    if (event.from == water && event.to == steam && why_boiled.empty()) why_boiled = cloud->explain(cloud->ids()[event.instance]);
                }
        }
        std::println("4: 243 drops of lava onto a pool of 81 of water: {} set as stone, {} of the water boiled off; lava left {}", set, boiled, count_of(*cloud, lava));
        expect(gt(set, std::size_t{0}) && gt(boiled, std::size_t{0})) << "stone and steam, from the same laws";
        std::println("   why the first water boiled:{}", said(*program, why_boiled));
        expect(!why_boiled.empty() && names(*program, why_boiled, "boils") && why_boiled.front().cause.from.carrier == particles::particles_carrier) << "explained: `boils`, the heat from a drop of the lava" << said(*program, why_boiled);
    };

    "5: in the frost, an ice block beside a fire melts and one left alone stays ice; a pail of water there freezes"_test = [] {
        const auto program = standard();
        const std::uint32_t temperature = *program->channel("temperature");
        const auto play = [&](std::string_view what, float kelvin, bool fire) {
            auto world = rigid3::World::make({});
            expect(fatal(world.has_value()));
            (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
            const rigid3::BodyId block = *world->add({.at = {0, 0.25f, 0}}, rigid3::Shape::box({0.25f, 0.25f, 0.25f}), {.density = 900});
            const rigid3::BodyId crate = *world->add({.at = {0.76f, 0.5f, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), {.density = 600});
            auto objects = rigid3::Objects::make(*program, {.ambient = *program->material("air"), .ambient_kelvin = kelvin});
            expect(fatal(objects.has_value()));
            (void)objects->add(block, *program->material(what));
            if (fire) (void)objects->add(crate, *program->material("wood"));
            int became = 0;
            for (int step = 1; step <= 1800 && !became; ++step) {
                world->step();
                if (fire && step < 60) (void)objects->set(crate, temperature, stored(*program, temperature, 900));
                expect(objects->step(*world).has_value());
                if (*objects->material(block) != *program->material(what)) became = step;
            }
            const auto chain = objects->explain(block);
            if (became) std::println("   why the {} became {}:{}", what, program->materials()[*objects->material(block)].name, said(*program, chain));
            expect(!became || names(*program, chain, fire ? "melts" : "freezes", fire ? std::optional{rigid3::who(crate)} : std::nullopt)) << "explained by the law that did it" << said(*program, chain);
            return std::pair{became, std::string(program->materials()[*objects->material(block)].name)};
        };
        const auto [melted_at, melted_into] = play("ice", 250, true);  // in the frost: only the fire can melt it
        const auto [frozen_at, frozen_into] = play("water", 250, false);
        const auto [kept_at, kept_as] = play("ice", 250, false);
        std::println("5: the ice beside the fire became {} at step {}; the water in the frost became {} at step {}; ice in the frost, left alone: {} ({})", melted_into, melted_at, frozen_into, frozen_at,
                     kept_as, kept_at);
        expect(gt(melted_at, 0) && melted_into == "water") << "it melted";
        expect(gt(frozen_at, 0) && frozen_into == "ice") << "it froze";
        expect(eq(kept_at, 0)) << "ice in the frost stays ice";
    };

    "6: oil on water, a few drops of it alight: the oil burns on the water, and the water does not"_test = [] {
        const auto program = standard();
        const matter::MaterialId water = *program->material("water"), oil = *program->material("oil");
        const std::uint32_t burning = *program->channel("burning");
        Tank tank({0.1f, 0.3f, 0.1f}, {0, 0.3f, 0});
        auto cloud = particles::Cloud::make(program, {.ambient = *program->material("air"), .touching = true});
        expect(fatal(cloud.has_value()));
        std::vector<particles::Spawn> spawns;
        layers(spawns, water, 0, 3, 0.1f);
        layers(spawns, oil, 3, 1, 0.1f);
        for (int drop = 0; drop < 4; ++drop)  // a few drops of it already alight (a match's worth)
            spawns.push_back({.at = {-0.01f + 0.02f * static_cast<float>(drop % 2), 0.1f, -0.01f + 0.02f * static_cast<float>(drop / 2)}, .life = 1000, .material = oil, .kelvin = 700.0f});
        cloud->emit(spawns);
        const std::size_t water_before = count_of(*cloud, water);
        std::size_t most_oil_burning = 0, water_burning = 0;
        std::vector<matter::Explained> why_caught;
        for (int step = 0; step < 600; ++step) {
            expect(cloud->step(tank.inside).has_value());
            for (const matter::Event& event : cloud->events())  // the first drop of oil to catch that was not lit at the start
                if (why_caught.empty() && event.kind == matter::EventKind::rose && event.channel == burning && cloud->material(event.instance) == oil && cloud->ids()[event.instance] < water_before + 81)
                    why_caught = cloud->explain(cloud->ids()[event.instance]);
            std::size_t oil_burning = 0;
            for (std::size_t particle = 0; particle < cloud->size(); ++particle)
                if (cloud->value(burning, particle) > 0) (cloud->material(particle) == oil ? oil_burning : water_burning) += 1;
            most_oil_burning = std::max(most_oil_burning, oil_burning);
        }
        std::println("6: oil on water, four drops of it alight: at most {} drops of oil burning at once, oil left {}; water burning ever {}; water {} → {}", most_oil_burning, count_of(*cloud, oil),
                     water_burning, water_before, count_of(*cloud, water));
        expect(gt(most_oil_burning, std::size_t{3})) << "the oil caught and burned";
        std::println("   why the first drop of oil caught:{}", said(*program, why_caught));
        expect(names(*program, why_caught, "burns") && !why_caught.empty() && why_caught.front().cause.from.carrier == particles::particles_carrier) << "explained: `burns`, with heat from another drop" << said(*program, why_caught);
        expect(eq(water_burning, std::size_t{0})) << "the water under it did not burn";
    };

    "7: heat goes along iron and hardly along stone (a bar of segments from a fire: its far end); charge put on an iron bar reaches the water at its end, on a wooden one it does not"_test = [] {
        const auto program = standard();
        const std::uint32_t temperature = *program->channel("temperature"), charge = *program->channel("charge");
        // A fire held at 1100 K and a bar of four 0.4 m segments from it along the ground; the far end's warmth at 30 s.
        const auto bar = [&](std::string_view of) {
            auto world = rigid3::World::make({});
            expect(fatal(world.has_value()));
            (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
            const rigid3::BodyId fire = *world->add({.at = {0, 0.25f, 0}}, rigid3::Shape::box({0.25f, 0.25f, 0.25f}), {.density = 600});
            std::vector<rigid3::BodyId> segments;
            for (int segment = 0; segment < 4; ++segment)
                segments.push_back(*world->add({.at = {0.451f + 0.401f * static_cast<float>(segment), 0.05f, 0}}, rigid3::Shape::box({0.2f, 0.05f, 0.05f}), {.density = 2000}));
            auto objects = rigid3::Objects::make(*program, {.ambient = *program->material("air")});
            expect(fatal(objects.has_value()));
            (void)objects->add(fire, *program->material("wood"));
            for (const rigid3::BodyId segment : segments) (void)objects->add(segment, *program->material(of));
            for (int step = 1; step <= 1800; ++step) {
                world->step();
                (void)objects->set(fire, temperature, stored(*program, temperature, 1100));
                expect(objects->step(*world).has_value());
            }
            return std::ldexp(static_cast<double>(*objects->get(segments.back(), temperature)), -8);
        };
        // A bar 2 m from a charge kept on it to a body of water at its end.
        const auto charged = [&](std::string_view of) {
            auto world = rigid3::World::make({});
            expect(fatal(world.has_value()));
            (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
            const rigid3::BodyId rod = *world->add({.at = {1, 0.05f, 0}}, rigid3::Shape::box({1, 0.05f, 0.05f}), {.density = 2000});
            const rigid3::BodyId pail = *world->add({.at = {2.25f, 0.25f, 0}}, rigid3::Shape::box({0.25f, 0.25f, 0.25f}), {.density = 1000});
            auto objects = rigid3::Objects::make(*program, {.ambient = *program->material("air")});
            expect(fatal(objects.has_value()));
            (void)objects->add(rod, *program->material(of)), (void)objects->add(pail, *program->material("water"));
            std::uint32_t most = 0;
            for (int step = 1; step <= 600; ++step) {
                world->step();
                (void)objects->set(rod, charge, 1000);
                expect(objects->step(*world).has_value());
                const std::uint32_t now = *objects->get(pail, charge);
                if (!most && now > 0) {
                    // The agent's question of the pail as its charge comes: whence? `charge flows`, from the rod.
                    const auto why = objects->why(pail);
                    const auto rod_at = static_cast<std::uint32_t>(std::ranges::find(objects->bodies(), rod) - objects->bodies().begin());
                    expect(why && std::ranges::any_of(why->flows, [&](const matter::Flowed& flow) { return flow.in && flow.beside == rod_at && program->laws()[flow.law].name == "charge flows"; }))
                        << "the pail's charge came in from the rod by `charge flows`";
                }
                most = std::max(most, now);
            }
            return most;
        };
        const double iron = bar("iron"), stone = bar("stone");
        const std::uint32_t iron_charge = charged("iron"), wood_charge = charged("wood");
        std::println("7: the far end of a 1.6 m bar from a fire, after 30 s: iron {:.1f} K, stone {:.1f} K; the water at the end of a charged iron bar {}, of a wooden one {}", iron, stone, iron_charge,
                     wood_charge);
        expect(gt(iron, stone + 30.0)) << "iron carries the heat along it";
        expect(gt(iron_charge, 0u) && eq(wood_charge, 0u)) << "charge along the iron and into the water, not along the wood";
    };

    "8: a keg of gunpowder set off between two crates throws them apart; a keg of wood burns where it stands"_test = [] {
        const auto program = standard();
        const std::uint32_t temperature = *program->channel("temperature");
        const auto play = [&](std::string_view keg_of) {
            auto world = rigid3::World::make({});
            expect(fatal(world.has_value()));
            (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
            const rigid3::BodyId keg = *world->add({.at = {0, 0.25f, 0}}, rigid3::Shape::box({0.25f, 0.25f, 0.25f}), {.density = 1700});
            const rigid3::BodyId left = *world->add({.at = {-0.56f, 0.25f, 0}}, rigid3::Shape::box({0.25f, 0.25f, 0.25f}), {.density = 300});
            const rigid3::BodyId right = *world->add({.at = {0.56f, 0.25f, 0}}, rigid3::Shape::box({0.25f, 0.25f, 0.25f}), {.density = 300});
            auto objects = rigid3::Objects::make(*program, {.ambient = *program->material("air")});
            expect(fatal(objects.has_value()));
            (void)objects->add(keg, *program->material(keg_of)), (void)objects->add(left, *program->material("wood")), (void)objects->add(right, *program->material("wood"));
            auto air = field::Field::make(*program, {.air = *program->material("air")});
            expect(fatal(air.has_value()));
            for (int step = 1; step <= 120; ++step) {
                objects->thrown(*world, *air);
                world->step();
                if (step < 10) (void)objects->set(keg, temperature, std::max(*objects->get(keg, temperature), stored(*program, temperature, 900)));  // a fuse
                expect(objects->step(*world, *air).has_value());
            }
            if (keg_of == "gunpowder") {
                const auto chain = objects->explain(keg);
                std::println("   why the keg is what it is:{}", said(*program, chain));
                expect(names(*program, chain, "burns out") && !chain.empty() && chain.back().cause.kind == matter::CauseKind::applied) << "burned out ← the fuse" << said(*program, chain);
            }
            return world->pose(right)->centre.x - world->pose(left)->centre.x;
        };
        const float powder = play("gunpowder"), wood = play("wood");
        std::println("8: two crates 1.12 m apart about a keg: after 2 s, {:.2f} m apart beside gunpowder, {:.2f} m beside wood", powder, wood);
        expect(gt(powder, 1.12f + 0.5f)) << "the blast threw them";
        expect(lt(std::abs(wood - 1.12f), 0.05f)) << "a wood fire pushes nothing";
    };

    "12: alcohol, two rows of properties and no law, is a liquid that burns: it comes up through water, boils on hot stones, its vapour rises and burns"_test = [] {
        auto definition = matter::read(matter::standard_snapshot, "standard.matter.omui.json");
        expect(fatal(definition.has_value()));
        using P = matter::Property;
        const std::size_t laws = definition->laws.size();
        definition->materials.push_back({"alcohol_vapour",
                                         {P{"phase", "gas"}, P{"density", 1.0}, P{"thermal", 1.0}, P{"boiling", 351.0}, P{"condensed", "alcohol"}, P{"starts_at", 360.0}, P{"ignition", 400.0},
                                          P{"fuel", 40.0}, P{"heat", 90.0}, P{"burnt", "smoke"}},
                                         {}});
        definition->materials.push_back({"alcohol", {P{"phase", "liquid"}, P{"density", 790.0}, P{"thermal", 1.0}, P{"boiling", 351.0}, P{"boiled", "alcohol_vapour"}}, {}});
        auto prepared = matter::Program::prepare(*definition);
        expect(fatal(prepared.has_value()));
        expect(eq(prepared->laws().size(), laws)) << "no law added";
        const auto program = std::make_shared<const matter::Program>(std::move(*prepared));
        const matter::MaterialId alcohol = *program->material("alcohol"), vapour = *program->material("alcohol_vapour"), water = *program->material("water");
        // Under water in a tank: it comes up.
        Tank tank({0.1f, 0.3f, 0.1f}, {0, 0.3f, 0});
        auto basin = particles::Cloud::make(program, {.ambient = *program->material("air"), .touching = true});
        expect(fatal(basin.has_value()));
        std::vector<particles::Spawn> spawns;
        layers(spawns, alcohol, 0, 1, 0.1f);
        layers(spawns, water, 1, 3, 0.1f);
        basin->emit(spawns);
        for (int step = 0; step < 300; ++step) expect(basin->step(tank.inside).has_value());
        double alcohol_height = 0, water_height = 0;
        for (std::size_t particle = 0; particle < basin->size(); ++particle)
            (basin->material(particle) == alcohol ? alcohol_height : water_height) += basin->at()[particle].y;
        alcohol_height /= static_cast<double>(count_of(*basin, alcohol)), water_height /= static_cast<double>(count_of(*basin, water));
        // On a bed of hot stones: it boils, the vapour rises and burns.
        auto pan = particles::Cloud::make(program, {.ambient = *program->material("air"), .touching = true});
        expect(fatal(pan.has_value()));
        spawns.clear();
        layers(spawns, *program->material("stone"), 0, 1, 0.1f, 700.0f);
        layers(spawns, alcohol, 1, 1, 0.1f);
        pan->emit(spawns);
        std::size_t boiled = 0, burned = 0;
        float highest = 0;
        const std::uint32_t burning = *program->channel("burning");
        std::vector<matter::Explained> why_vapour;
        for (int step = 0; step < 600; ++step) {
            expect(pan->step(tank.inside).has_value());
            for (const matter::Event& event : pan->events()) {
                boiled += event.kind == matter::EventKind::became && event.from == alcohol && event.to == vapour;
                if (why_vapour.empty() && event.kind == matter::EventKind::became && event.from == alcohol && event.to == vapour) why_vapour = pan->explain(pan->ids()[event.instance]);
            }
            for (std::size_t particle = 0; particle < pan->size(); ++particle)
                if (pan->material(particle) == vapour) highest = std::max(highest, pan->at()[particle].y), burned += pan->value(burning, particle) > 0;
        }
        std::println("12: alcohol under water came up to a mean height {:.3f} m (water {:.3f}); on hot stones {} boiled, the vapour as high as {:.2f} m, burning in {} particle-steps", alcohol_height,
                     water_height, boiled, highest, burned);
        expect(gt(alcohol_height, water_height)) << "it lies on water";
        expect(gt(boiled, std::size_t{0}) && gt(highest, 0.1f)) << "it boils and its vapour rises";
        expect(gt(burned, std::size_t{0})) << "and the vapour burns";
        std::println("   why the first alcohol boiled:{}", said(*program, why_vapour));
        expect(names(*program, why_vapour, "boils")) << "explained by `boils`, a law it shares with water" << said(*program, why_vapour);
    };
}};
