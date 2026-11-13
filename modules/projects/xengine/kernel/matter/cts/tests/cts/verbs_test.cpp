// What is done to a world of matter, as the engine's parts (xengine.cellular.verbs; the foundation plan's §6.15,
// T2.14e, D-183): an explosion, a heat, a pour, a vent. Each is shown to work by the laws alone — it places
// matter only where there is none and applies quantities — and the world's count of cells a work overwrote
// stays where it was.
import std;
import xengine.matter.json;
import xengine.cellular.verbs;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace bodies = xengine::cellular::rigid;
namespace verbs = xengine::cellular::verbs;

namespace {

const matter::Program& standard() {
    static const matter::Program program = *matter::standard();
    return program;
}
constexpr ca::Shape shape{64, 40};
std::unique_ptr<bodies::MatterWorld> room() {
    auto world = *bodies::MatterWorld::make(standard(), {.shape = shape, .tile = {16, 16}, .seed = 3, .fill = "air", .flying = {.thrown = true}});
    world->place({0, 36, 64, 4}, *standard().material("stone"));
    return world;
}
std::uint32_t count(const bodies::MatterWorld& world, std::string_view name, ca::Tile region = {0, 0, shape.width, shape.height}) {
    const std::uint32_t material = *standard().material(name);
    std::uint32_t found = 0;
    for (std::uint32_t y = region.y; y < region.y + region.height; ++y)
        for (std::uint32_t x = region.x; x < region.x + region.width; ++x) found += world.cells()[y * shape.width + x] == material;
    return found;
}

}  // namespace

static UTest cases{"cellular.verbs", [] {
    "an explosion is a pressure and a heat: a wall of stone by it is rubble, by the law that shatters; the verb turned nothing into anything"_test = [] {
        auto world = room();
        world->place({30, 20, 3, 16}, *standard().material("stone"));
        const std::uint32_t stone = count(*world, "stone");
        const std::uint64_t set_up = world->converted();
        verbs::explode(*world, {27, 30}, {.radius = 6, .pressure = 600000, .heat_radius = 3, .kelvin = 1100});
        for (int frame = 0; frame < 240; ++frame) world->step();
        const std::uint32_t sand = count(*world, "sand") + static_cast<std::uint32_t>(world->flying().flying());
        std::println("an explosion by a wall: {} of stone → {}, {} of sand; converted by the work {}", stone, count(*world, "stone"), sand, world->converted() - set_up);
        expect(gt(sand, 20u) && eq(count(*world, "stone") + sand, stone)) << "what the wall lost is sand, cell for cell";
        expect(eq(world->converted(), set_up)) << "the verb placed nothing";
    };
    "a heat lights what burns: wood heated past its ignition burns to ash; stone beside it is stone"_test = [] {
        auto world = room();
        world->place({20, 30, 6, 6}, *standard().material("wood"));
        const std::uint64_t set_up = world->converted();
        verbs::heat(*world, {23, 33}, 2, 1000);
        for (int frame = 0; frame < 1500; ++frame) world->step();
        std::println("wood heated: {} of wood left, {} of ash", count(*world, "wood"), count(*world, "ash"));
        expect(lt(count(*world, "wood"), 36u) && gt(count(*world, "ash"), 0u));
        expect(eq(count(*world, "stone"), 64u * 4u) && eq(world->converted(), set_up));
    };
    "a pour fills only where there is gas: what is there stays; and placing over what is there is counted"_test = [] {
        auto world = room();
        const std::uint32_t water = *standard().material("water");
        const std::uint64_t set_up = world->converted();
        // On the floor: of the 5 × 5 square cut to 3 steps, the rows in the stone are not filled.
        const std::uint32_t filled = verbs::pour(*world, {40, 35}, water, {.radius = 2, .reach = 3});
        expect(eq(filled, 13u) && eq(count(*world, "water"), 13u)) << filled;
        expect(eq(count(*world, "stone"), 64u * 4u) && eq(world->converted(), set_up)) << "the floor is the floor";
        expect(eq(verbs::pour(*world, {40, 35}, water, {.radius = 2, .reach = 3}), 0u)) << "where it poured there is no gas left";
        world->place({40, 36, 2, 1}, water);
        expect(eq(world->converted(), set_up + 2)) << "a work that writes over stone is seen to";
    };
    "a vent gives without end: a spring fills a basin, a frame at a time"_test = [] {
        auto world = room();
        world->place({20, 28, 1, 8}, *standard().material("stone")), world->place({34, 28, 1, 8}, *standard().material("stone"));
        const verbs::Vent spring{.at = {27, 22}, .material = *standard().material("water"), .every = 4};
        std::uint32_t given = 0;
        for (int frame = 0; frame < 400; ++frame) given += verbs::vent(*world, spring), world->step();
        const std::uint32_t in_basin = count(*world, "water", {21, 28, 13, 8});
        std::println("a spring over a basin: {} cells given in 400 frames, {} in the basin", given, in_basin);
        expect(eq(given, 100u) && ge(in_basin, 80u));
    };
}};
