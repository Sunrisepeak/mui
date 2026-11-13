// Matter that is only for show (xengine.cellular's feature `decor`; D-178): a layer beside a world of matter,
// stepped by the same laws, that reads the world and is never read by it. Scenery placed in the layer catches
// from a fire that is the world's and burns; the world is the same bits with the layer or without — its every
// frame's hash — because nothing of the layer reaches it.
import std;
import xengine.matter.json;
import xengine.cellular.rigid;
import xengine.cellular.decor;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace bodies = xengine::cellular::rigid;
namespace decor = xengine::cellular::decor;

namespace {

// A cavern: a floor of stone, a post of wood alight at its foot — the world's own fire.
std::unique_ptr<bodies::MatterWorld> cavern(const matter::Program& program) {
    auto world = *bodies::MatterWorld::make(program, {.shape = {64, 48}, .tile = {16, 16}, .seed = 5, .fill = "air"});
    world->place({0, 44, 64, 4}, *program.material("stone"));
    world->place({20, 32, 3, 12}, *program.material("wood"));
    world->set(*program.channel("temperature"), {20, 42, 3, 2}, 1000 * 256);
    return world;
}
std::size_t count_of(const matter::Program& program, std::span<const std::uint32_t> cells, std::size_t area, std::string_view channel) {
    const std::uint32_t at = *program.channel(channel);
    std::size_t count = 0;
    for (std::size_t cell = 0; cell < area; ++cell) count += cells[at * area + cell] != 0;
    return count;
}

}  // namespace

static UTest cases{"matter.decor", [] {
    "scenery placed in the layer catches from the world's fire and burns; the world never has it"_test = [] {
        const matter::Program program = *matter::standard();
        const auto world = cavern(program);
        auto layer = *decor::Decor::make(program, world->shape(), {.seed = 9, .fill = "air"});
        const std::size_t area = std::size_t{64} * 48;
        const std::uint32_t wood = *program.material("wood");
        expect(eq(layer.place({23, 40, 8, 4}, wood), 0u)) << "before its first step the layer has no field";
        layer.step(world->cells());
        // A hedge beside the post: the layer's alone.
        expect(eq(layer.place({23, 40, 8, 4}, wood), 32u));
        expect(layer.owns(25, 41) && !layer.owns(21, 41));
        expect(eq(layer.place({0, 45, 4, 1}, wood), 0u)) << "where the world has stone, nothing is placed";
        expect(eq(layer.place({40, 20, 2, 2}, *program.material("water")), 0u)) << "scenery is solid";
        std::size_t most_alight = 0, most_differing = 0;
        bool world_had_it = false;
        for (int frame = 0; frame < 900; ++frame) {
            world->step();
            layer.step(world->cells());
            const auto cells = world->cells();
            for (std::uint32_t y = 40; y < 44; ++y)
                for (std::uint32_t x = 23; x < 31; ++x) world_had_it = world_had_it || cells[std::size_t{y} * 64 + x] == wood;
            std::size_t alight = 0;
            const std::uint32_t burning = *program.channel("burning");
            for (std::uint32_t y = 40; y < 44; ++y)
                for (std::uint32_t x = 23; x < 31; ++x) alight += layer.owns(x, y) && layer.cells()[burning * area + std::size_t{y} * 64 + x] != 0;
            most_alight = std::max(most_alight, alight);
            most_differing = std::max<std::size_t>(most_differing, layer.last(world->cells()).differing);
        }
        expect(!world_had_it) << "the hedge is in the layer; the world's cells there were never wood";
        expect(most_alight >= 4u) << std::format("at most {} cells of the hedge were alight", most_alight);
        expect(most_differing >= 32u) << std::format("at most {} cells differed", most_differing);
        const decor::Statistics last = layer.last();
        expect(last.owned < 32u) << std::format("{} cells of the hedge left after the fire: it burned", last.owned);
        // Where the world's own post is, the layer shows the world's cells (a few may be a step behind: they
        // follow at the next).
        expect(count_of(program, world->cells(), area, "burning") <= count_of(program, layer.cells(), area, "burning") + 64u);
    };

    "the world is the same bits with a layer beside it or without: its hash at every frame"_test = [] {
        const matter::Program program = *matter::standard();
        const auto alone = cavern(program), shown = cavern(program);
        auto layer = *decor::Decor::make(program, shown->shape(), {.seed = 9, .fill = "air"});
        layer.step(shown->cells());
        (void)layer.place({23, 40, 8, 4}, *program.material("wood"));
        (void)layer.place({40, 30, 10, 2}, *program.material("wood"));
        for (int frame = 0; frame < 600; ++frame) {
            alone->step();
            shown->step();
            layer.step(shown->cells());
            if (frame == 300) layer.restart(shown->cells(), 30);  // a restart in the middle changes nothing of the world either
            expect(fatal(eq(alone->state_hash(), shown->state_hash()))) << std::format("frame {}", frame);
        }
    };

    "a layer is the same bits from the same frames; restarted, it is the world again with its scenery as first placed"_test = [] {
        const matter::Program program = *matter::standard();
        const auto run = [&](int frames) {
            const auto world = cavern(program);
            auto layer = *decor::Decor::make(program, world->shape(), {.seed = 9, .fill = "air"});
            layer.step(world->cells());
            (void)layer.place({23, 40, 8, 4}, *program.material("wood"));
            for (int frame = 0; frame < frames; ++frame) world->step(), layer.step(world->cells());
            return std::pair{std::vector<std::uint32_t>(layer.cells().begin(), layer.cells().end()), std::vector<std::uint32_t>(world->cells().begin(), world->cells().end())};
        };
        expect(run(400).first == run(400).first);
        // Where nothing is placed and the world is all there is, the layer is the world.
        const auto world = cavern(program);
        auto layer = *decor::Decor::make(program, world->shape(), {.seed = 9, .fill = "air"});
        for (int frame = 0; frame < 50; ++frame) world->step(), layer.step(world->cells());
        (void)layer.place({23, 40, 8, 4}, *program.material("wood"));
        for (int frame = 0; frame < 200; ++frame) world->step(), layer.step(world->cells());
        layer.restart(world->cells());
        expect(eq(layer.last().owned + layer.last().followed, std::uint64_t{64} * 48)) << "restarted: every cell the world's";
        std::size_t hedge = 0;
        for (std::uint32_t y = 40; y < 44; ++y)
            for (std::uint32_t x = 23; x < 31; ++x) hedge += layer.owns(x, y);
        expect(hedge > 0u) << "its scenery is placed again where the world leaves room";
    };
}};
