// The lattice as a carrier of matter (xengine.cellular's feature `matter`): a matter program lowered to a
// lattice program. First through the carrier suite — the reference step's bits, every tile run and sparsely —
// then with what the reference has no say in: cells that change places by their density and their phase.
// No rule here names a material: sand, water, oil, steam and what was added a moment ago move by two columns.
import std;
import xengine.matter.cts;
import xengine.matter.json;
import xengine.cellular.matter;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace suite = xengine::matter::cts;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
using mlibs::cts::Status;

namespace {

ca::Boundary edge_of(suite::Grid grid) { return grid.wrapped ? ca::Boundary{ca::Edge::wrap, 0} : ca::nothing_beyond; }

// The lattice with every tile run, in tiles of that size.
suite::Subject lattice(std::string name, ca::Shape tile) {
    return {.name = std::move(name), .carry = [tile](const matter::Program& program, suite::Grid grid) -> suite::Step {
                auto plan = ca::lower(program, {.moves = false});
                if (!plan) return {};
                return [plan = std::move(*plan), grid, tile](std::span<const std::uint32_t> state, std::span<std::uint32_t> next) {
                    return plan.step({grid.width, grid.height}, state, next, {.tile = tile, .boundary = edge_of(grid)}).has_value();
                };
            }};
}
// The lattice run sparsely: what is at rest sleeps, and is the same bits.
suite::Subject sparse_lattice(std::string name, ca::Shape tile, std::shared_ptr<std::uint64_t> slept = {}) {
    return {.name = std::move(name), .carry = [tile, slept](const matter::Program& program, suite::Grid grid) -> suite::Step {
                auto plan = ca::lower(program, {.moves = false});
                if (!plan) return {};
                const ca::Shape tiles = ca::tiles({grid.width, grid.height}, tile);
                auto activity = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(tiles.width) * tiles.height, std::uint8_t{0});
                return [plan = std::move(*plan), grid, tile, activity, slept](std::span<const std::uint32_t> state, std::span<std::uint32_t> next) {
                    const auto stepped = plan.step({grid.width, grid.height}, state, next, {.tile = tile, .boundary = edge_of(grid), .activity = *activity});
                    if (stepped && slept) *slept += stepped->asleep;
                    return stepped.has_value();
                };
            }};
}

// A world of matter on the lattice, cells moving: read by material and channel name.
struct World {
    matter::Program program;
    ca::Program plan;
    ca::Shape shape;
    std::vector<std::uint32_t> cells, next;
    std::vector<std::uint8_t> activity;
    ca::Shape tile{4, 4};
    std::uint64_t tick = 0, run = 0;
    bool sparse = false;

    World(matter::Program of, ca::Shape size, std::string_view filled, std::uint32_t seed = 7)
        : program(std::move(of)), plan(*ca::lower(program, {.seed = seed})), shape(size), cells(program.fresh(area(), *program.material(filled))), next(cells.size()) {
        const ca::Shape tiles = ca::tiles(shape, tile);
        activity.assign(static_cast<std::size_t>(tiles.width) * tiles.height, 0);
    }
    [[nodiscard]] std::size_t area() const { return static_cast<std::size_t>(shape.width) * shape.height; }
    [[nodiscard]] std::size_t at(std::uint32_t x, std::uint32_t y) const { return static_cast<std::size_t>(y) * shape.width + x; }
    void put(std::uint32_t x, std::uint32_t y, std::string_view material) {
        program.place(cells, area(), at(x, y), *program.material(material));
        plan.wake(shape, tile, {x, y, 1, 1}, activity);
    }
    void fill(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::string_view material) {
        for (std::uint32_t y = y0; y <= y1; ++y)
            for (std::uint32_t x = x0; x <= x1; ++x) put(x, y, material);
    }
    [[nodiscard]] std::uint32_t& value(std::string_view channel, std::uint32_t x, std::uint32_t y) { return cells[*program.channel(channel) * area() + at(x, y)]; }
    void heat(std::uint32_t x, std::uint32_t y, std::uint32_t kelvins) {
        value("temperature", x, y) = kelvins * 256;
        plan.wake(shape, tile, {x, y, 1, 1}, activity);
    }
    [[nodiscard]] std::string_view material(std::uint32_t x, std::uint32_t y) const { return program.materials()[cells[at(x, y)]].name; }
    [[nodiscard]] bool is(std::uint32_t x, std::uint32_t y, std::string_view name) const { return material(x, y) == name; }
    [[nodiscard]] std::size_t count(std::string_view name) const {
        return static_cast<std::size_t>(std::ranges::count(std::span(cells).first(area()), *program.material(name)));
    }
    // The mean height of a material's cells, from the top (row 0): what rose is nearer 0.
    [[nodiscard]] double depth(std::string_view name) const {
        double sum = 0;
        std::size_t found = 0;
        for (std::uint32_t y = 0; y < shape.height; ++y)
            for (std::uint32_t x = 0; x < shape.width; ++x)
                if (is(x, y, name)) sum += y, ++found;
        return found ? sum / static_cast<double>(found) : -1;
    }
    ca::Statistics step(int count = 1) {
        ca::Statistics last;
        for (int index = 0; index < count; ++index) {
            const auto stepped = plan.step(shape, cells, next, {.tile = tile, .boundary = ca::nothing_beyond, .tick = tick++, .activity = sparse ? std::span<std::uint8_t>(activity) : std::span<std::uint8_t>{}});
            expect(fatal(stepped.has_value()));
            last = *stepped;
            run += stepped->cells;
            cells.swap(next);
        }
        return last;
    }
    [[nodiscard]] std::string picture() const {
        std::string out;
        for (std::uint32_t y = 0; y < shape.height; ++y) {
            for (std::uint32_t x = 0; x < shape.width; ++x) {
                const std::string_view name = material(x, y);
                out += name == "air" ? '.' : name == "stone" ? '#' : name == "water" ? '~' : name == "steam" ? '^' : name == "smoke" ? '%' : name[0];
            }
            out += '\n';
        }
        return out;
    }
};

matter::Program standard() { return *matter::standard(); }

}  // namespace

UTest cases{"matter.lattice", [] {
    // The carrier suite on the lattice, as this program's own cases: every tile run, in small tiles, and sparsely.
    suite::declare(lattice("lattice", {32, 32}));
    suite::declare(lattice("lattice-tiles", {3, 2}));
    suite::declare(sparse_lattice("lattice-sparse", {4, 4}));

    "the lattice is the reference step's bits: every case, whole, in tiles and sparsely — and what is at rest did sleep"_test = [] {
        const auto whole = mlibs::cts::tally([] { suite::declare(lattice("lattice", {32, 32})); });
        expect(whole.ok() && eq(whole.count(Status::passed), suite::case_count())) << whole.text();
        const auto slept = std::make_shared<std::uint64_t>(0);
        const auto sparse = mlibs::cts::tally([&] { suite::declare(sparse_lattice("lattice-sparse", {4, 4}, slept)); });
        expect(sparse.ok() && eq(sparse.count(Status::passed), suite::case_count())) << sparse.text();
        expect(*slept > 0u) << "tiles slept in the suite's worlds";
        std::println("the carrier suite on the sparse lattice: {} cases the reference's bits, {} tile-steps asleep", sparse.count(Status::passed), *slept);
    };

    "a lattice wrong in one thing fails the suite: another neighbourhood, an edge that is something"_test = [] {
        const auto diagonal = mlibs::cts::tally([] {
            suite::declare({.name = "lattice-of-eight", .carry = [](const matter::Program& program, suite::Grid grid) -> suite::Step {
                                // Eight neighbours each: heat's share of a difference to each of four no longer leaves room.
                                auto plan = ca::lower(program, {.neighbourhood = {ca::surrounding.begin(), ca::surrounding.end()}, .moves = false});
                                if (!plan) return {};
                                return [plan = std::move(*plan), grid](std::span<const std::uint32_t> state, std::span<std::uint32_t> next) {
                                    return plan.step({grid.width, grid.height}, state, next, {.boundary = edge_of(grid)}).has_value();
                                };
                            }});
        });
        expect(!diagonal.ok()) << diagonal.text();
        const auto walled = mlibs::cts::tally([] {
            suite::declare({.name = "lattice-in-air", .carry = [](const matter::Program& program, suite::Grid grid) -> suite::Step {
                                auto plan = ca::lower(program, {.moves = false});
                                if (!plan) return {};
                                // Beyond the edge: material 0 at 0 K, not nothing.
                                return [plan = std::move(*plan), grid](std::span<const std::uint32_t> state, std::span<std::uint32_t> next) {
                                    return plan.step({grid.width, grid.height}, state, next, {.boundary = {ca::Edge::constant, 0}}).has_value();
                                };
                            }, .has = {"grid"}});
        });
        expect(!walled.ok() && walled.count(Status::failed) >= 5u) << walled.text();
    };

    "the lowered program: the table is the materials' properties and how each may move; the laws its transfers and reactions"_test = [] {
        const matter::Program program = standard();
        const auto plan = ca::lower(program);
        expect(fatal(plan.has_value()));
        expect(eq(plan->encoding(), 3u) && plan->reacts() && plan->moves());
        expect(eq(plan->rows(), static_cast<std::uint32_t>(program.materials().size())));
        expect(eq(plan->channels(), static_cast<std::uint32_t>(program.channels().size())));
        expect(eq(plan->transfer_count(), 3u)) << "heat, charge, wetness";
        expect(eq(plan->reach(), 1u));
        std::println("the standard definition on the lattice: {} words for {} materials of {} columns and {} laws", plan->words().size(), program.materials().size(),
                     program.columns().size(), program.laws().size());
        expect(eq(ca::mobility_of(matter::Phase::solid), 0u) && eq(ca::mobility_of(matter::Phase::soft), 0u));
        expect(eq(ca::mobility_of(matter::Phase::granular), ca::falls | ca::slides) && eq(ca::mobility_of(matter::Phase::liquid), ca::falls | ca::slides | ca::spreads) &&
               eq(ca::mobility_of(matter::Phase::gas), ca::falls | ca::spreads));
        const auto still = ca::lower(program, {.moves = false});
        expect(fatal(still.has_value()));
        expect(!still->moves());
    };

    "by density and phase alone: sand sinks through water and piles, oil lies on water, steam and smoke go up, stone and wood stay"_test = [] {
        World world(standard(), {16, 14}, "air");
        world.fill(0, 13, 15, 13, "stone");
        world.fill(0, 9, 15, 12, "water");
        world.fill(6, 0, 8, 2, "sand");     // dropped from above
        world.fill(1, 10, 3, 11, "oil");    // let go under water
        world.fill(11, 12, 13, 12, "smoke");  // let go at the bottom
        world.put(14, 5, "wood");           // a solid: it is dynamics' to move
        const std::size_t sand = world.count("sand"), oil = world.count("oil"), smoke = world.count("smoke"), water = world.count("water");
        const double smoke_before = world.depth("smoke"), oil_before = world.depth("oil");
        world.step(400);
        expect(eq(world.count("sand"), sand) && eq(world.count("oil"), oil) && eq(world.count("smoke"), smoke) && eq(world.count("water"), water)) << "every cell kept\n" << world.picture();
        for (std::uint32_t y = 0; y < 13; ++y)
            for (std::uint32_t x = 0; x < 16; ++x) {
                if (world.is(x, y, "sand")) expect(!world.is(x, y + 1, "water") && !world.is(x, y + 1, "air") && !world.is(x, y + 1, "oil")) << std::format("sand rests on sand or stone at {}, {}\n", x, y) << world.picture();
                if (world.is(x, y, "oil")) expect(!world.is(x, y + 1, "air")) << "oil rests on what is heavier";
                if (world.is(x, y, "water")) expect(!world.is(x, y + 1, "oil") && !world.is(x, y + 1, "air") && !world.is(x, y + 1, "smoke")) << std::format("nothing lighter under water at {}, {}\n", x, y) << world.picture();
            }
        expect(world.depth("oil") < oil_before && world.depth("oil") < world.depth("water")) << "oil came up and lies on the water";
        expect(world.depth("smoke") < 2.0 && world.depth("smoke") < smoke_before) << std::format("smoke is at the top (mean row {})", world.depth("smoke"));
        expect(world.is(14, 5, "wood")) << "a solid stays where it is";
        for (std::uint32_t x = 0; x < 16; ++x) expect(world.is(x, 13, "stone"));
        std::println("sand, water, oil and smoke by density and phase, after 400 ticks:\n{}", world.picture());
    };

    "laws and movement together: water over lava boils, the steam rises and condenses where the air is still cool; the lava sets as stone"_test = [] {
        World world(standard(), {12, 12}, "air");
        world.fill(0, 11, 11, 11, "stone");
        world.fill(0, 4, 0, 10, "stone");
        world.fill(11, 4, 11, 10, "stone");
        world.fill(1, 9, 10, 10, "lava");
        world.fill(1, 6, 10, 8, "water");
        std::size_t most_steam = 0;
        double highest_steam = 99;
        bool rained = false;
        for (int step = 0; step < 1500; ++step) {
            world.step();
            const std::size_t steam = world.count("steam");
            most_steam = std::max(most_steam, steam);
            if (steam) highest_steam = std::min(highest_steam, world.depth("steam"));
            // Water above where any was put: it went up as steam and condensed there.
            for (std::uint32_t y = 0; y < 5 && !rained; ++y)
                for (std::uint32_t x = 0; x < 12 && !rained; ++x) rained = world.is(x, y, "water");
        }
        std::println("water over lava on 12 x 12: at most {} cells of steam at once, its mean row as high as {:.1f}; {} stone, {} lava, {} water, {} steam after 1500 ticks\n{}", most_steam,
                     highest_steam, world.count("stone"), world.count("lava"), world.count("water"), world.count("steam"), world.picture());
        expect(most_steam >= 3u) << "water boiled";
        expect(highest_steam < 6.0) << "and the steam rose above where the water was";
        expect(rained) << "and condensed up there";
        expect(world.count("lava") < 20u) << "lava set as stone where the water took its heat";
        expect(eq(world.count("water") + world.count("steam") + world.count("ice"), std::size_t{30})) << "water in all its phases is what there was";
    };

    "a fire on the lattice: wood burns piece by piece, oil burns to smoke that rises; nothing burns without air beside it"_test = [] {
        World world(standard(), {14, 8}, "air");
        world.fill(0, 7, 13, 7, "stone");
        world.fill(1, 6, 8, 6, "wood");
        world.fill(10, 6, 12, 6, "oil");
        world.fill(9, 5, 9, 6, "stone");  // a wall between them
        world.fill(13, 5, 13, 6, "stone");
        world.heat(1, 6, 700);
        world.heat(10, 6, 600);
        std::size_t most_smoke = 0;
        double highest_smoke = 99;
        for (int step = 0; step < 900; ++step) {
            world.step();
            if (const std::size_t smoke = world.count("smoke")) {
                most_smoke = std::max(most_smoke, smoke);
                highest_smoke = std::min(highest_smoke, world.depth("smoke"));
            }
        }
        std::println("wood and oil alight on 14 x 8, after 900 ticks: {} ash, {} wood, {} oil, {} smoke (its mean row as high as {:.1f})\n{}", world.count("ash"), world.count("wood"),
                     world.count("oil"), world.count("smoke"), highest_smoke, world.picture());
        expect(eq(world.count("wood"), std::size_t{0}) && eq(world.count("ash"), std::size_t{8})) << "every piece of wood burnt to ash";
        expect(eq(world.count("oil"), std::size_t{0}) && eq(most_smoke, std::size_t{3})) << "the oil burnt to smoke";
        expect(highest_smoke < 3.0) << "which rose";
        // A fire is as hot as its flame: the stone under it and the wall beside it are stone still.
        for (std::uint32_t x = 0; x < 14; ++x) expect(world.is(x, 7, "stone")) << "the floor did not melt";
        expect(world.is(9, 5, "stone") && world.is(9, 6, "stone") && eq(world.count("lava"), std::size_t{0}));
        // Sealed in stone, hot enough: no oxygen beside it, nothing burns.
        World sealed(standard(), {5, 5}, "stone");
        sealed.put(2, 2, "wood");
        sealed.heat(2, 2, 900);
        sealed.step(300);
        expect(sealed.is(2, 2, "wood") && eq(sealed.value("burned", 2, 2), 0u));
    };

    "a material added as rows takes part in movement too: alcohol lies on water, boils beside a fire, its vapour rises"_test = [] {
        auto definition = matter::read(matter::standard_snapshot, "standard.matter.omui.json");
        expect(fatal(definition.has_value()));
        using P = matter::Property;
        const std::size_t laws = definition->laws.size();
        definition->materials.push_back({"alcohol_vapour",
                                         {P{"phase", "gas"}, P{"density", 1.0}, P{"thermal", 1.0}, P{"boiling", 351.0}, P{"condensed", "alcohol"}, P{"starts_at", 360.0}, P{"ignition", 400.0},
                                          P{"fuel", 40.0}, P{"heat", 90.0}, P{"burnt", "smoke"}},
                                         {}});
        definition->materials.push_back({"alcohol", {P{"phase", "liquid"}, P{"density", 790.0}, P{"thermal", 1.0}, P{"boiling", 351.0}, P{"boiled", "alcohol_vapour"}}, {}});
        auto program = matter::Program::prepare(*definition);
        expect(fatal(program.has_value()));
        expect(eq(program->laws().size(), laws) && eq(program->recipes(), std::size_t{0}));
        // Let go under water in a basin: it comes up and lies on it.
        World basin(*program, {10, 8}, "air");
        basin.fill(0, 7, 9, 7, "stone");
        basin.fill(0, 4, 9, 6, "water");
        basin.fill(3, 6, 6, 6, "alcohol");
        basin.step(300);
        expect(eq(basin.count("alcohol"), std::size_t{4}) && basin.depth("alcohol") < basin.depth("water")) << basin.picture();
        for (std::uint32_t x = 0; x < 10; ++x) expect(!basin.is(x, 6, "alcohol")) << "none left at the bottom";
        // A hot stone under it: it boils to its own vapour, which rises.
        World pan(*program, {8, 10}, "air");
        pan.fill(0, 9, 7, 9, "stone");
        pan.fill(2, 8, 5, 8, "alcohol");
        for (std::uint32_t x = 0; x < 8; ++x) pan.heat(x, 9, 700);
        double highest = 99;
        std::size_t most = 0;
        for (int step = 0; step < 400; ++step) {
            pan.step();
            if (const std::size_t vapour = pan.count("alcohol_vapour")) {
                most = std::max(most, vapour);
                highest = std::min(highest, pan.depth("alcohol_vapour"));
            }
        }
        std::println("alcohol over a hot stone: at most {} cells of vapour at once, as high as row {:.1f}", most, highest);
        expect(most >= 1u && highest < 7.0) << pan.picture();
    };

    "what no one wrote: sand dropped into a full basin pushes water over its wall, the water runs to the wood — and the wood will not light"_test = [] {
        // Two worlds, the same but for nine grains of sand. No law says sand, basin or overflow: sand is heavier
        // than water and changes places with it, water levels, wood takes up the water beside it, and what is wet
        // has a higher ignition point.
        const auto build = [](bool with_sand) {
            World world(standard(), {32, 16}, "air");
            world.fill(0, 15, 31, 15, "stone");
            world.fill(1, 12, 1, 14, "stone");
            world.fill(10, 12, 10, 14, "stone");
            world.fill(2, 12, 9, 14, "water");  // to the brim
            world.fill(20, 14, 27, 14, "wood");
            if (with_sand) world.fill(4, 2, 6, 4, "sand");
            return world;
        };
        World dry = build(false), wet = build(true);
        dry.step(300);
        wet.step(300);
        std::size_t spilled = 0;
        for (std::uint32_t x = 11; x < 32; ++x) spilled += wet.is(x, 14, "water");
        expect(spilled >= 5u && eq(wet.count("water"), std::size_t{24})) << "the water the sand displaced went over the wall\n" << wet.picture();
        for (std::uint32_t x = 11; x < 32; ++x) expect(!dry.is(x, 14, "water")) << "and none did without the sand";
        expect(wet.value("wetness", 20, 14) > 0u && eq(dry.value("wetness", 20, 14), 0u)) << "the wood it reached is wet";
        std::println("nine grains of sand into a full basin, after 300 ticks ({} cells of water over the wall, the nearest wood's wetness {}):\n{}", spilled, wet.value("wetness", 20, 14),
                     wet.picture());
        // The same flame to both.
        dry.heat(20, 14, 700);
        wet.heat(20, 14, 700);
        dry.step(900);
        wet.step(900);
        expect(eq(dry.count("wood"), std::size_t{0}) && eq(dry.count("ash"), std::size_t{8})) << "the dry wood burnt, every piece\n" << dry.picture();
        expect(eq(wet.count("wood"), std::size_t{8}) && eq(wet.count("ash"), std::size_t{0})) << "the wet wood did not catch\n" << wet.picture();
    };

    "sparse — a world of matter at rest sleeps, a fire keeps awake what it reaches, and the cells are the bits of every tile run"_test = [] {
        const auto build = [](bool sparse) {
            World world(standard(), {32, 16}, "air");
            world.sparse = sparse;
            world.fill(0, 15, 31, 15, "stone");
            world.fill(2, 12, 9, 14, "water");
            world.fill(1, 9, 1, 14, "stone");  // a basin deep enough for the water the sand pushes up
            world.fill(10, 9, 10, 14, "stone");
            world.fill(20, 14, 27, 14, "wood");
            world.fill(4, 2, 6, 4, "sand");
            return world;
        };
        World dense = build(false), sparse = build(true);
        std::uint64_t asleep_before_fire = 0;
        for (int step = 0; step < 300; ++step) {
            dense.step();
            asleep_before_fire = sparse.step().asleep;
            if (dense.cells != sparse.cells) {
                expect(false) << std::format("step {}: the sparse field parts from the dense one", step);
                return;
            }
        }
        expect(asleep_before_fire > 0u) << "at rest, tiles sleep";
        const std::uint64_t run_at_rest = sparse.run;
        // Light the wood in both.
        dense.heat(20, 14, 700);
        sparse.heat(20, 14, 700);
        std::uint64_t least_asleep = 99;
        for (int step = 0; step < 900; ++step) {
            dense.step();
            least_asleep = std::min(least_asleep, sparse.step().asleep);
            if (dense.cells != sparse.cells) {
                expect(false) << std::format("step {} after the fire: the sparse field parts from the dense one", step);
                return;
            }
        }
        expect(eq(sparse.count("wood"), std::size_t{0})) << sparse.picture();
        expect(least_asleep < asleep_before_fire) << "the fire woke tiles";
        expect(sparse.run < dense.run) << "and fewer cells were run than all";
        std::println("32 x 16 of matter in tiles of 4 x 4: {} of 32 tiles asleep at rest; {} cells run of {} over 1200 ticks ({} of them before the fire)", asleep_before_fire, sparse.run,
                     dense.run, run_at_rest);
    };

    "events between two states, as the reference names them: what became another material, what caught and went out"_test = [] {
        const matter::Program program = standard();
        const auto plan = ca::lower(program, {.moves = false});
        expect(fatal(plan.has_value()));
        const suite::Grid grid{6, 3, false};
        std::vector<std::uint32_t> state = program.fresh(18, *program.material("stone")), next(state.size()), reference_next(state.size());
        for (std::uint32_t x = 0; x < 6; ++x) program.place(state, 18, x, *program.material("air"));
        for (std::uint32_t x = 0; x < 6; ++x) program.place(state, 18, 6 + x, *program.material("wood"));
        state[*program.channel("temperature") * 18 + 6] = 700 * 256;
        const auto pairs = suite::pairs_of(grid);
        matter::Workspace workspace;
        std::size_t compared = 0, became = 0, rose = 0;
        for (int step = 0; step < 800; ++step) {
            std::vector<matter::Event> said, seen;
            expect(fatal(program.step({18, pairs}, state, reference_next, workspace, &said).has_value()));
            expect(fatal(plan->step({6, 3}, state, next, {.boundary = ca::nothing_beyond}).has_value()));
            ca::events_between(program, 18, state, next, seen);
            expect(fatal(said.size() == seen.size())) << std::format("step {}: {} events, the reference's {}", step, seen.size(), said.size());
            for (std::size_t index = 0; index < said.size(); ++index) {
                const matter::Event &one = said[index], &other = seen[index];
                expect(one.kind == other.kind && one.instance == other.instance && one.channel == other.channel && one.from == other.from && one.to == other.to) << std::format("step {}, event {}", step, index);
                // A channel's event has its name here too; which law made a thing another is the commit's to say (T1.11).
                if (one.kind != matter::EventKind::became) expect(eq(one.name, other.name));
                became += one.kind == matter::EventKind::became;
                rose += one.kind == matter::EventKind::rose;
                ++compared;
            }
            state.swap(next);
        }
        expect(eq(became, std::size_t{6}) && eq(rose, std::size_t{6})) << std::format("{} events: {} became, {} rose", compared, became, rose);
    };
}};
