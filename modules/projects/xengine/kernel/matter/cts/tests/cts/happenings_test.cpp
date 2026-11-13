// What a step's laws did that has a name, said by the step itself (cellular's Execution::happened, a world's
// `happened` and `events`; the foundation plan's §6.15, T2.14f, D-183): a cell became another thing, a flag rose —
// with the law, the event's name and the material it was, and with nobody watching the world. It is what a
// sound or a score listens to.
import std;
import xengine.matter.json;
import xengine.cellular.rigid;
import xengine.cellular.matter;
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
struct Heard {
    std::uint64_t frame = 0;
    matter::Event event;
};
// The world run `frames` frames, and every event of every step.
std::vector<Heard> listen(bodies::MatterWorld& world, int frames) {
    std::vector<Heard> heard;
    std::vector<matter::Event> events;
    for (int frame = 0; frame < frames; ++frame) {
        world.step();
        events.clear();
        world.events(events);
        for (const matter::Event& event : events) heard.push_back({world.frames(), event});
    }
    return heard;
}
std::string_view name_of(const matter::Event& event) {
    return event.name < standard().event_names().size() ? std::string_view(standard().event_names()[event.name]) : std::string_view("(unnamed)");
}
std::uint32_t named(std::span<const Heard> heard, std::string_view name) {
    return static_cast<std::uint32_t>(std::ranges::count_if(heard, [&](const Heard& one) { return name_of(one.event) == name; }));
}

}  // namespace

static UTest cases{"cellular.happenings", [] {
    "a world at rest says nothing; an unwatched world says what its laws did"_test = [] {
        auto world = room();
        expect(!world->watched());
        expect(eq(listen(*world, 30).size(), std::size_t{0})) << "stone on a floor: nothing became anything";
    };
    "a wall shattered is heard as it shatters: the law, the event, stone to sand, in the frames after the bomb"_test = [] {
        auto world = room();
        const std::uint32_t stone = *standard().material("stone"), sand = *standard().material("sand");
        world->place({30, 20, 3, 16}, stone);
        verbs::explode(*world, {27, 30}, {.radius = 6, .pressure = 600000, .heat_radius = 3, .kelvin = 1100});
        const std::vector<Heard> heard = listen(*world, 240);
        std::uint32_t shattered = 0, wrong = 0;
        std::uint64_t first = 0, last = 0;
        for (const Heard& one : heard) {
            if (name_of(one.event) != "shatter") continue;
            if (!shattered++) first = one.frame;
            last = one.frame;
            wrong += one.event.kind != matter::EventKind::became || one.event.from != stone || one.event.to != sand ||
                     standard().laws()[one.event.law].name != "shatters" || one.event.instance >= shape.width * shape.height;
        }
        std::println("a wall by a bomb: {} events, {} of them shatter (frames {}–{})", heard.size(), shattered, first, last);
        expect(gt(shattered, 10u) && eq(wrong, 0u));
        expect(le(first, std::uint64_t{3})) << "the pressure is past the stone's toughness at once";
    };
    "wood lit is heard to ignite: the flag's rise, by the law that burns, of wood and nothing else"_test = [] {
        auto world = room();
        const std::uint32_t wood = *standard().material("wood");
        world->place({20, 30, 6, 6}, wood);
        verbs::heat(*world, {23, 33}, 2, 1000);
        const std::vector<Heard> heard = listen(*world, 600);
        std::uint32_t wrong = 0;
        for (const Heard& one : heard)
            if (name_of(one.event) == "ignite") wrong += one.event.kind != matter::EventKind::rose || one.event.from != 0 || standard().laws()[one.event.law].name != "burns";
        std::println("wood heated: {} events, {} ignite", heard.size(), named(heard, "ignite"));
        expect(gt(named(heard, "ignite"), 5u) && eq(wrong, 0u));
    };
    "the same world says the same things: a branch run beside it, event for event"_test = [] {
        auto world = room();
        world->place({30, 20, 3, 16}, *standard().material("stone")), world->place({10, 30, 6, 6}, *standard().material("wood"));
        verbs::explode(*world, {27, 30}, {.radius = 6, .pressure = 600000, .heat_radius = 3, .kelvin = 1100});
        verbs::heat(*world, {13, 33}, 2, 1000);
        for (int frame = 0; frame < 5; ++frame) world->step();
        auto other = *world->branch();
        expect(eq(other->happened().size(), std::size_t{0})) << "what was heard is not of the world's state";
        const std::vector<Heard> first = listen(*world, 200), second = listen(*other, 200);
        bool same = first.size() == second.size();
        for (std::size_t index = 0; same && index < first.size(); ++index)
            same = first[index].frame == second[index].frame && first[index].event.instance == second[index].event.instance &&
                   first[index].event.law == second[index].event.law && first[index].event.kind == second[index].event.kind;
        expect(same && gt(first.size(), std::size_t{10})) << first.size() << " and " << second.size();
        expect(eq(world->state_hash(), other->state_hash()));
    };
    "the lattice's own step says it, the same list whichever way its tiles are run; spans of another size are refused"_test = [] {
        const matter::Program& program = standard();
        const ca::Program plan = *ca::lower(program, {.seed = 3});
        const ca::Shape tile{8, 8}, tiles = ca::tiles(shape, tile);
        const std::size_t area = std::size_t{shape.width} * shape.height, tile_count = std::size_t{tiles.width} * tiles.height;
        const std::uint32_t temperature = *program.channel("temperature"), fraction = program.channels()[temperature].fraction;
        struct Run {
            bool reversed = false;
            std::vector<std::uint32_t> cells, next;
            std::vector<ca::Happening> happened;
            std::vector<std::uint8_t> counts;
            ca::Statistics last;
        };
        std::vector<std::uint32_t> initial = program.fresh(area, *program.material("air"));
        for (std::uint32_t y = 10; y < 30; ++y)
            for (std::uint32_t x = 12; x < 52; ++x) {
                initial[std::size_t{y} * shape.width + x] = *program.material(x % 7 < 4 ? "wood" : "ice");
                initial[temperature * area + std::size_t{y} * shape.width + x] = (x % 7 < 4 ? 900u : 320u) << fraction;
            }
        std::array<Run, 2> runs{{{false, initial, std::vector<std::uint32_t>(initial.size()), std::vector<ca::Happening>(tile_count * ca::happenings_a_tile), std::vector<std::uint8_t>(tile_count), {}},
                                 {true, initial, std::vector<std::uint32_t>(initial.size()), std::vector<ca::Happening>(tile_count * ca::happenings_a_tile), std::vector<std::uint8_t>(tile_count), {}}}};
        const std::vector<std::uint32_t> laws = ca::laws_of_reactions(program);
        std::uint64_t kept = 0, all = 0;
        std::map<std::string, std::uint32_t> said;
        for (std::uint64_t tick = 0; tick < 120; ++tick) {
            for (Run& run : runs) {
                const auto stepped = plan.step(shape, run.cells, run.next, {.tile = tile, .boundary = ca::nothing_beyond, .reverse_tiles = run.reversed, .happened = run.happened, .happened_counts = run.counts, .tick = tick});
                expect(fatal(stepped.has_value()));
                run.last = *stepped;
                run.cells.swap(run.next);
            }
            bool same = runs[0].cells == runs[1].cells && runs[0].counts == runs[1].counts && runs[0].last.happenings == runs[1].last.happenings;
            for (std::size_t at = 0; same && at < tile_count; ++at)
                for (std::size_t place = 0; same && place < runs[0].counts[at]; ++place) {
                    const ca::Happening &one = runs[0].happened[at * ca::happenings_a_tile + place], &other = runs[1].happened[at * ca::happenings_a_tile + place];
                    same = one.cell == other.cell && one.reaction == other.reaction && one.was == other.was && one.rose == other.rose;
                    const matter::Event event = ca::event_of(program, laws, one);
                    ++said[std::format("{} {}", name_of(event), program.materials()[event.material].name)];
                    ++kept;
                }
            expect(fatal(same)) << "tick " << tick;
            all += runs[0].last.happenings;
        }
        std::string text;
        for (const auto& [name, count] : said) text += std::format("{}{} × {}", text.empty() ? "" : ", ", name, count);
        std::println("hot wood and warm ice, 120 steps: {} happenings, {} kept in their tiles: {}", all, kept, text);
        expect(gt(said["ignite wood"], 20u) && gt(said["melt ice"], 20u)) << text;
        expect(ge(all, kept) && gt(kept, std::uint64_t{0}));
        // Not listened to: the same cells.
        Run unheard{false, initial, std::vector<std::uint32_t>(initial.size()), {}, {}, {}};
        for (std::uint64_t tick = 0; tick < 120; ++tick) {
            expect(fatal(plan.step(shape, unheard.cells, unheard.next, {.tile = tile, .boundary = ca::nothing_beyond, .tick = tick}).has_value()));
            unheard.cells.swap(unheard.next);
        }
        expect(unheard.cells == runs[0].cells) << "listening changes nothing";
        std::vector<std::uint8_t> short_counts(tile_count - 1);
        expect(!plan.step(shape, unheard.cells, unheard.next, {.tile = tile, .happened = runs[0].happened, .happened_counts = short_counts}).has_value());
    };
}};
