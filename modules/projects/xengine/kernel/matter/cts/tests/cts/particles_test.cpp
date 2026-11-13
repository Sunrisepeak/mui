// Cells of the lattice in flight (xengine.cellular's feature `particles`; the plan's §6.7, lattice ↔ particles),
// in a world of matter with bodies (MatterWorld): a cell thrown flies an arc and is a cell again where it lands;
// gunpowder going off inside a heap of sand throws the sand by the pressure it makes — no law or scene for it;
// a blast breaks a wall when told to; nothing is made or lost, and the same frames are the same bits.
import std;
import xengine.matter.json;
import xengine.cellular.rigid;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;
namespace bodies = xengine::cellular::rigid;
namespace flying = xengine::cellular::particles;

namespace {
const matter::Program& standard() {
    static const matter::Program program = *matter::standard();
    return program;
}
std::unique_ptr<bodies::MatterWorld> room(std::uint32_t width, std::uint32_t height, bool thrown = false) {
    auto world = *bodies::MatterWorld::make(standard(), {.shape = {width, height}, .tile = {8, 8}, .fill = "air", .flying = {.thrown = thrown}});
    world->place({0, height - 2, width, 2}, *standard().material("stone"));
    return world;
}
// A material's cells in the field, and those of it in flight.
std::size_t count(bodies::MatterWorld& world, std::string_view name) {
    const auto material = *standard().material(name);
    const auto area = static_cast<std::size_t>(world.shape().width) * world.shape().height;
    std::size_t found = static_cast<std::size_t>(std::ranges::count(world.cells().first(area), material));
    for (std::size_t particle = 0; particle < world.flying().flying(); ++particle) found += world.flying().value(particle, 0) == material;
    return found;
}
// The columns a material's cells span.
std::pair<std::uint32_t, std::uint32_t> span_of(bodies::MatterWorld& world, std::string_view name) {
    const auto material = *standard().material(name);
    std::uint32_t left = world.shape().width, right = 0;
    for (std::uint32_t y = 0; y < world.shape().height; ++y)
        for (std::uint32_t x = 0; x < world.shape().width; ++x)
            if (world.cells()[std::size_t{y} * world.shape().width + x] == material) left = std::min(left, x), right = std::max(right, x);
    return {left, right};
}
}  // namespace

static UTest cases{"matter.particles", [] {
    "a cell of sand thrown flies an arc and is sand again where it lands — where a stone would"_test = [] {
        auto world = room(96, 48);
        world->place({10, 40, 1, 1}, *standard().material("sand"));
        // A blast below and to the left of it: thrown up and to the right (cells of 0.1 m, gravity 9.8).
        expect(fatal(world->blast({9.0f, 41.5f}, 3.0f, 8.0f) == 1u)) << "the one cell of sand is thrown, not the stone below it";
        expect(eq(world->flying().flying(), std::size_t{1}));
        int frames = 0;
        for (; frames < 600 && world->flying().flying() > 0; ++frames) world->step();
        expect(fatal(eq(world->flying().flying(), std::size_t{0}))) << "it landed";
        expect(eq(count(*world, "sand"), std::size_t{1})) << "one cell of sand, as before";
        const auto [left, right] = span_of(*world, "sand");
        expect(eq(left, right) && right > 11u) << std::format("landed at column {}, to the right", right);
    };

    "gunpowder going off inside a heap of sand throws the sand by the pressure it makes, and every grain comes down"_test = [] {
        auto world = room(96, 48, true);
        world->place({38, 34, 20, 12}, *standard().material("sand"));
        world->place({46, 42, 4, 3}, *standard().material("gunpowder"));
        const std::size_t sand = count(*world, "sand");
        const auto [left_before, right_before] = span_of(*world, "sand");
        world->set(*standard().channel("temperature"), {46, 42, 4, 3}, 900 * 256);
        std::uint64_t thrown = 0;
        std::size_t most = 0;
        bool conserved = true;
        for (int frame = 0; frame < 900; ++frame) {
            world->step();
            thrown += world->flying().last().thrown;
            most = std::max(most, world->flying().flying());
            conserved = conserved && count(*world, "sand") == sand;
        }
        expect(thrown > 20u) << std::format("{} cells thrown, {} in flight at once at most", thrown, most);
        expect(conserved) << "every grain is in the field or in the air, every frame";
        expect(eq(world->flying().flying(), std::size_t{0})) << "and all came down";
        const auto [left, right] = span_of(*world, "sand");
        expect(right - left > right_before - left_before + 6) << std::format("the heap spread from {}..{} to {}..{}", left_before, right_before, left, right);
    };

    "a blast breaks a stone wall when told to, and leaves it when not: what flies is what the wall was"_test = [] {
        for (const bool breaks : {false, true}) {
            auto world = room(64, 40);
            world->place({30, 10, 4, 28}, *standard().material("stone"));
            const std::size_t stone = count(*world, "stone");
            const auto thrown = world->blast({31.5f, 30.0f}, 4.0f, 12.0f, breaks);
            if (!breaks) {
                expect(eq(thrown, 0u)) << "stone stays";
                continue;
            }
            expect(thrown > 20u) << std::format("{} cells of the wall thrown", thrown);
            for (int frame = 0; frame < 600 && world->flying().flying() > 0; ++frame) world->step();
            expect(eq(world->flying().flying(), std::size_t{0}));
            expect(eq(count(*world, "stone"), stone)) << "the stone is all still there, some of it elsewhere";
            const auto material = *standard().material("stone");
            std::size_t holes = 0;
            for (std::uint32_t y = 27; y <= 33; ++y)
                for (std::uint32_t x = 30; x <= 33; ++x) holes += world->cells()[std::size_t{y} * 64 + x] != material;
            expect(holes > 10u) << std::format("a hole in the wall: {} cells", holes);
        }
    };

    "the same frames with things in flight are the same bits; a world saved in mid-flight, restored, flies on the same"_test = [] {
        const auto run = [](bool interrupt) {
            auto world = room(96, 48, true);
            world->place({38, 34, 20, 12}, *standard().material("sand"));
            world->place({46, 42, 4, 3}, *standard().material("gunpowder"));
            world->set(*standard().channel("temperature"), {46, 42, 4, 3}, 900 * 256);
            for (int frame = 0; frame < 120; ++frame) world->step();
            if (interrupt) {
                const auto saved = world->save();
                for (int frame = 0; frame < 30; ++frame) world->step();
                (void)world->restore(saved);
            }
            for (int frame = 0; frame < 120; ++frame) world->step();
            return world->state_hash();
        };
        expect(eq(run(false), run(true)));
    };
}};
