// Rigid bodies made of the lattice's cells (xengine.cellular's feature `rigid`; the plan's §6.7): Box2D moves
// them, the lattice keeps simulating their cells. All of the standard definition, no scene adding a law: a crate
// of wood lands on stone, floats in water where one of iron sinks (by the table's densities), burns away and
// falls in two; matter is conserved and two runs are the same bits.
import std;
import xengine.matter.json;
import xengine.cellular.rigid;
import mlibs.simulation;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace bodies = xengine::cellular::rigid;
namespace dyn = xengine::dynamics::rigid;

namespace {

// A world of matter as a model of mlibs.simulation: what a game's state holds. An input pushes the bodies.
struct Model {
    using State = bodies::WorldValue;
    struct Input { float push = 0; };
    static void step(State& world, const Input& input) {
        if (input.push != 0)
            for (const auto& view : world->bodies().bodies()) world->rigid().apply(view.body, {input.push, 0}, 0);
        world->step();
    }
};

// A world of matter with rigid bodies in it, open to air at 293 K. Made in place: the bodies hold the world.
struct Scene {
    matter::Program program;
    ca::Program plan;
    ca::Shape shape;
    std::vector<std::uint32_t> cells, next, outside;
    dyn::World world;
    bodies::Bodies coupled;
    std::uint64_t tick = 0;

    Scene(ca::Shape size)
        : program(*matter::standard()), plan(*ca::lower(program, {.seed = 3})), shape(size), cells(program.fresh(area(), *program.material("air"))), next(cells.size()),
          outside(program.fresh(1, *program.material("air"))), world(*dyn::World::make({.gravity = {0, 9.8f}})),
          coupled(*bodies::Bodies::make(program, shape, world, {.cell = 0.1f})) {}
    Scene(const Scene&) = delete;
    [[nodiscard]] std::size_t area() const { return static_cast<std::size_t>(shape.width) * shape.height; }
    void fill(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::string_view material) {
        for (std::uint32_t y = y0; y <= y1; ++y)
            for (std::uint32_t x = x0; x <= x1; ++x) program.place(cells, area(), std::size_t{y} * shape.width + x, *program.material(material));
    }
    [[nodiscard]] std::vector<dyn::BodyId> carve(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
        return *coupled.carve(cells, {x0, y0, x1 - x0 + 1, y1 - y0 + 1}, outside);
    }
    void frame() {
        (void)plan.step(shape, cells, next, {.tile = {16, 16}, .boundary = ca::open_to(outside), .tick = tick++});
        cells.swap(next);
        coupled.push(cells);
        coupled.lift(cells);
        coupled.terrain(cells);
        world.step();
        coupled.stamp(cells);
    }
    [[nodiscard]] std::size_t count(std::string_view name) const {
        return static_cast<std::size_t>(std::ranges::count(std::span(cells).first(area()), *program.material(name)));
    }
    // The rows a body covers in the field now: its top and bottom.
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> rows(dyn::BodyId body) const {
        std::uint32_t top = shape.height, bottom = 0;
        for (std::uint32_t y = 0; y < shape.height; ++y)
            for (std::uint32_t x = 0; x < shape.width; ++x)
                if (const auto owner = coupled.owner(x, y); owner && *owner == body) top = std::min(top, y), bottom = std::max(bottom, y);
        return {top, bottom};
    }
};

}  // namespace

UTest cases{"matter.rigid", [] {
    "a crate of wood falls onto stone and rests on it, every cell of it kept"_test = [] {
        Scene scene({48, 40});
        scene.fill(0, 36, 47, 39, "stone");
        scene.fill(20, 5, 27, 12, "wood");
        const auto made = scene.carve(20, 5, 27, 12);
        expect(fatal(eq(made.size(), std::size_t{1})));
        for (int frame = 0; frame < 180; ++frame) scene.frame();
        const auto crate = scene.coupled.body(made[0]);
        expect(fatal(crate.has_value()));
        expect(eq(crate->cells, 64u));
        const auto [top, bottom] = scene.rows(made[0]);
        expect(eq(bottom, 35u)) << std::format("it rests on the floor: rows {} to {}", top, bottom);
        expect(eq(top, 28u));
        expect(eq(scene.count("wood"), std::size_t{64})) << "its cells are cells of the field";
        expect(eq(scene.count("stone"), std::size_t{48 * 4})) << "the floor is the floor";
        const auto pose = scene.world.pose(made[0]);
        expect(std::abs(pose->velocity.y) < 0.05f) << std::format("at rest: {} m/s", pose->velocity.y);
    };

    "in a basin of water a crate of wood floats and one of iron sinks — the table's densities — and not a cell of water is lost"_test = [] {
        Scene scene({64, 48});
        scene.fill(0, 44, 63, 47, "stone");
        scene.fill(0, 10, 1, 43, "stone");
        scene.fill(62, 10, 63, 43, "stone");
        scene.fill(2, 24, 61, 43, "water");
        scene.fill(10, 10, 19, 15, "wood");
        scene.fill(40, 10, 45, 15, "iron");
        const std::size_t water = scene.count("water");
        const auto made = scene.carve(2, 2, 61, 20);
        expect(fatal(eq(made.size(), std::size_t{2})));
        const dyn::BodyId wood = made[0], iron = made[1];
        std::uint64_t hidden = 0, displaced = 0;
        for (int frame = 0; frame < 600; ++frame) {
            scene.frame();
            hidden += scene.coupled.last().hidden;
            displaced += scene.coupled.last().displaced;
        }
        expect(displaced > 0u) << "the bodies moved water aside";
        expect(eq(hidden, std::uint64_t{0})) << "always into a gas near it";
        expect(eq(scene.count("water"), water)) << "every cell of water is still in the field";
        const auto [wood_top, wood_bottom] = scene.rows(wood);
        const auto [iron_top, iron_bottom] = scene.rows(iron);
        // The surface: the highest row holding water.
        std::uint32_t surface = scene.shape.height;
        for (std::uint32_t y = 0; y < scene.shape.height && surface == scene.shape.height; ++y)
            for (std::uint32_t x = 2; x < 62; ++x)
                if (scene.cells[std::size_t{y} * scene.shape.width + x] == *scene.program.material("water")) { surface = y; break; }
        expect(wood_top < surface && wood_bottom >= surface) << std::format("wood floats across the surface (row {}): rows {} to {}", surface, wood_top, wood_bottom);
        expect(eq(iron_bottom, 43u)) << std::format("iron lies on the bottom: rows {} to {}", iron_top, iron_bottom);
        const auto pose = scene.world.pose(wood);
        expect(std::abs(pose->velocity.y) < 0.2f) << std::format("the wood has come to rest: {} m/s", pose->velocity.y);
    };

    "a plank across two pillars, lit in the middle, burns through there and falls in two: a body each side"_test = [] {
        // Air above and below the middle (a fire needs oxygen around it; one lying on stone burns from the top only).
        Scene scene({64, 40});
        scene.fill(0, 36, 63, 39, "stone");
        scene.fill(14, 22, 18, 35, "stone");
        scene.fill(45, 22, 49, 35, "stone");
        scene.fill(15, 20, 48, 21, "wood");
        const auto made = scene.carve(15, 20, 48, 21);
        expect(fatal(eq(made.size(), std::size_t{1})));
        const auto temperature = *scene.program.channel("temperature");
        for (std::uint32_t y = 20; y <= 21; ++y)
            for (std::uint32_t x = 31; x <= 32; ++x) scene.cells[temperature * scene.area() + std::size_t{y} * scene.shape.width + x] = 900 * 256;
        std::uint64_t lost = 0, split = 0;
        std::size_t frames = 0;
        for (; frames < 2000 && split == 0; ++frames) {
            scene.frame();
            lost += scene.coupled.last().lost;
            split += scene.coupled.last().split;
        }
        expect(fatal(split >= 1u)) << std::format("it fell in two after {} frames ({} cells lost)", frames, lost);
        const auto all = scene.coupled.bodies();
        expect(all.size() >= 2u);
        std::uint32_t left_most = scene.shape.width, right_most = 0;
        for (const auto& view : all) {
            expect(view.cells > 0u);
            const auto pose = scene.world.pose(view.body);
            left_most = std::min(left_most, static_cast<std::uint32_t>(pose->centre.x / 0.1f));
            right_most = std::max(right_most, static_cast<std::uint32_t>(pose->centre.x / 0.1f));
        }
        expect(left_most < 31u && right_most > 32u) << std::format("one each side of the fire: {} and {}", left_most, right_most);
        std::println("the plank fell in two after {} frames, {} cells burnt away", frames, lost);
    };

    "a world of matter with bodies saved, run on, restored and run again is the same bits; a branch runs on as it does; sparse is dense"_test = [] {
        static const matter::Program program = *matter::standard();
        // A basin with a crate of wood falling in, a plank burning over it, sand pouring: everything moving.
        const auto make = [](bool sparse) {
            auto world = *bodies::MatterWorld::make(program, {.shape = {64, 48}, .tile = {8, 8}, .seed = 5, .fill = "air", .sparse = sparse});
            const auto material = [&](std::string_view name) { return *program.material(name); };
            world->place({0, 44, 64, 4}, material("stone"));
            world->place({0, 20, 2, 24}, material("stone"));
            world->place({62, 20, 2, 24}, material("stone"));
            world->place({2, 32, 60, 12}, material("water"));
            world->place({10, 6, 8, 8}, material("wood"));
            world->place({30, 18, 24, 2}, material("wood"));
            world->place({40, 2, 6, 6}, material("sand"));
            (void)world->carve({2, 2, 60, 18});
            world->set(*program.channel("temperature"), {30, 18, 2, 2}, 900 * 256);
            return world;
        };
        auto world = make(true);
        for (int frame = 0; frame < 60; ++frame) world->step();
        const auto saved = world->save();
        auto branch = world->branch();
        expect(fatal(branch.has_value()));
        std::vector<std::uint64_t> ahead;
        for (int frame = 0; frame < 120; ++frame) world->step(), ahead.push_back(world->state_hash());
        expect(fatal(world->restore(saved).has_value()));
        expect(eq(world->frames(), std::uint64_t{60}));
        bool again = true, branched = true;
        for (std::size_t frame = 0; frame < 120; ++frame) {
            world->step();
            (*branch)->step();
            again = again && world->state_hash() == ahead[frame];
            branched = branched && (*branch)->state_hash() == ahead[frame];
        }
        expect(again) << "restored and run again: the same bits frame by frame";
        expect(branched) << "the branch ran on as the original did";
        expect(world->bodies().bodies().size() >= 2u) << "bodies are in it";
        // The same world with every tile run: sparse execution and the bodies' wakes change no bit.
        auto sparse = make(true), dense = make(false);
        bool same = true;
        for (int frame = 0; frame < 240 && same; ++frame) {
            sparse->step(), dense->step();
            same = sparse->state_hash() == dense->state_hash();
            if (!same) expect(false) << std::format("frame {}: sparse parts from dense", frame);
        }
    };

    "a world of matter is a value a timeline keeps: pushed, played, sought back and played again to the same bits"_test = [] {
        static const matter::Program program = *matter::standard();
        auto made = *bodies::MatterWorld::make(program, {.shape = {48, 32}, .tile = {8, 8}, .fill = "air"});
        made->place({0, 28, 48, 4}, *program.material("stone"));
        made->place({2, 18, 44, 10}, *program.material("water"));
        made->place({8, 4, 6, 6}, *program.material("wood"));
        made->place({30, 4, 4, 4}, *program.material("iron"));
        (void)made->carve({0, 0, 48, 12});
        mlibs::simulation::Timeline<Model> timeline(bodies::WorldValue(std::move(made)), 240, 60);
        const auto input_at = [](std::int64_t frame) { return Model::Input{.push = frame == 50 ? 20.0f : 0.0f}; };
        std::vector<std::uint64_t> hashes;
        for (std::int64_t frame = 0; frame < 200; ++frame) timeline.step(input_at(frame)), hashes.push_back(timeline.state()->state_hash());
        expect(fatal(timeline.seek(100)));
        expect(eq(timeline.state()->state_hash(), hashes[99])) << "sought to frame 100";
        bool same = true;
        for (std::int64_t frame = 100; frame < 200; ++frame) {
            timeline.step(input_at(frame));
            same = same && timeline.state()->state_hash() == hashes[static_cast<std::size_t>(frame)];
        }
        expect(same) << "played again from 100: the same bits";
        expect(hashes[60] != hashes[40]) << "the world moved";
    };

    "two runs of the same scene are the same bits: the field, every body"_test = [] {
        std::array<std::uint64_t, 2> states{};
        std::array<std::vector<std::uint32_t>, 2> fields;
        for (int run = 0; run < 2; ++run) {
            Scene scene({64, 48});
            scene.fill(0, 44, 63, 47, "stone");
            scene.fill(2, 30, 61, 43, "water");
            scene.fill(10, 10, 19, 15, "wood");
            scene.fill(30, 4, 33, 20, "wood");
            (void)scene.carve(2, 2, 61, 25);
            for (int frame = 0; frame < 240; ++frame) scene.frame();
            states[static_cast<std::size_t>(run)] = scene.world.state_hash();
            fields[static_cast<std::size_t>(run)] = scene.cells;
        }
        expect(eq(states[0], states[1]));
        expect(fields[0] == fields[1]);
    };
}};
