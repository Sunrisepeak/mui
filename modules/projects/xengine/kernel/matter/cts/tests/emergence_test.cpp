// The emergence set (the plan's §9.3), its 2D part — 1 to 8 and 12 — on the lattice: each a scene with no
// interface and assertions on its state, all of the one standard definition (standard.matter.omui.ts). None of
// its laws names a material and no scene adds a law: what happens follows from properties — a density, a phase,
// an ignition point, how well a thing conducts — and from what is beside what. The numbers are the cases'
// titles in the plan's table; the last cases are what the table does not list and the laws gave anyway.
import std;
import xengine.matter.json;
import xengine.cellular.matter;
import xengine.dynamics.matter;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace dyn = xengine::dynamics;

namespace {

// A world of matter on the lattice, cells moving, open to air at 293 K beyond its edges: read by material and
// channel name.
struct World {
    matter::Program program;
    ca::Program plan;
    ca::Shape shape;
    std::vector<std::uint32_t> cells, next;
    std::vector<std::uint8_t> activity;
    ca::Shape tile{4, 4};
    std::uint64_t tick = 0, run = 0;
    bool sparse = false;
    // What is beyond the edges: air at the room's temperature — heat leaves to it, a fire at the edge has its oxygen.
    std::vector<std::uint32_t> outside;
    bool closed = false;  // a closed room: nothing beyond its edges, and it keeps its heat

    World(matter::Program of, ca::Shape size, std::string_view filled, std::uint32_t seed = 7)
        : program(std::move(of)), plan(*ca::lower(program, {.seed = seed})), shape(size), cells(program.fresh(area(), *program.material(filled))), next(cells.size()),
          outside(program.fresh(1, *program.material("air"))) {
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
    // Every cell at that temperature: a cold room, a hot one.
    void room(std::uint32_t kelvins) {
        std::ranges::fill(std::span(cells).subspan(*program.channel("temperature") * area(), area()), kelvins * 256);
        outside[*program.channel("temperature")] = kelvins * 256;
        std::ranges::fill(activity, std::uint8_t{0});
    }
    [[nodiscard]] double kelvin(std::uint32_t x, std::uint32_t y) { return value("temperature", x, y) / 256.0; }
    // How many cells are burning.
    [[nodiscard]] std::size_t alight() const {
        return static_cast<std::size_t>(std::ranges::count_if(std::span(cells).subspan(*program.channel("burning") * area(), area()), [](std::uint32_t value) { return value != 0; }));
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
            const auto stepped = plan.step(shape, cells, next, {.tile = tile, .boundary = closed ? ca::nothing_beyond : ca::open_to(outside), .tick = tick++, .activity = sparse ? std::span<std::uint8_t>(activity) : std::span<std::uint8_t>{}});
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

UTest cases{"matter.emergence", [] {
    "1 — sand falls into a pile, water finds its level, oil lies on water (and smoke goes up, stone and wood stay): by density and phase alone"_test = [] {
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

    "2 — wood lit at one end burns along the strip, each piece from the last; wet wood is harder to light"_test = [] {
        World world(standard(), {12, 5}, "air");
        world.fill(0, 4, 11, 4, "stone");
        world.fill(2, 3, 9, 3, "wood");
        world.heat(2, 3, 700);
        std::array<int, 8> caught;
        caught.fill(-1);
        int steps = 0;
        for (; steps < 1500 && world.count("wood"); ++steps) {
            world.step();
            for (std::uint32_t piece = 0; piece < 8; ++piece)
                if (caught[piece] < 0 && world.is(2 + piece, 3, "wood") && world.value("burning", 2 + piece, 3)) caught[piece] = steps + 1;
        }
        std::println("2: a strip of 8 of wood on the lattice caught at ticks {}, all ash after {}", caught, steps);
        expect(eq(caught[0], 1)) << "the lit end is alight at once";
        for (std::size_t piece = 1; piece < 8; ++piece) expect(caught[piece] > caught[piece - 1]) << std::format("piece {} caught at {}, the one before at {}", piece, caught[piece], caught[piece - 1]);
        expect(eq(world.count("ash"), std::size_t{8}) && eq(world.count("wood"), std::size_t{0})) << world.picture();
        // Two pieces at 600 K, one of them wet: 570 K lights dry wood; each of wetness is 40 K more.
        World pair(standard(), {9, 4}, "air");
        pair.fill(0, 3, 8, 3, "stone");
        pair.put(2, 2, "wood");
        pair.put(6, 2, "wood");
        pair.value("wetness", 6, 2) = 10;
        pair.heat(2, 2, 600);
        pair.heat(6, 2, 600);
        pair.step(3);
        expect(pair.value("burned", 2, 2) > 0u && eq(pair.value("burned", 6, 2), 0u)) << "the dry piece burns, the wet one does not";
    };

    "3 — water puts a fire out and becomes steam; the steam rises and comes down as water where it is cool"_test = [] {
        // A strip of wood alight in a stone trough; in one of two worlds water is let go above it, two for each
        // piece: enough to cover the wood, little enough that the fire's heat boils some of it (water takes 540 K
        // to boil once it is at 373: a pond over this fire would make no steam at all).
        const auto build = [] {
            World world(standard(), {24, 18}, "air");
            world.fill(0, 17, 23, 17, "stone");
            world.fill(7, 12, 7, 16, "stone");
            world.fill(16, 12, 16, 16, "stone");
            world.fill(8, 16, 15, 16, "wood");
            for (std::uint32_t x = 8; x <= 15; ++x) world.heat(x, 16, 700);
            return world;
        };
        World dry = build(), doused = build();
        dry.step(250);  // a fire well under way: the trough's stone is hot too
        doused.step(250);
        expect(eq(doused.alight(), std::size_t{8})) << "the whole strip is alight";
        doused.fill(8, 8, 15, 9, "water");
        const std::size_t water = doused.count("water");
        std::size_t most_steam = 0;
        std::uint32_t highest_steam = 99;
        int out_at = -1;
        for (int step = 0; step < 900; ++step) {
            doused.step();
            most_steam = std::max(most_steam, doused.count("steam"));
            for (std::uint32_t y = 0; y < highest_steam && y < 18; ++y)
                for (std::uint32_t x = 0; x < 24; ++x)
                    if (doused.is(x, y, "steam")) highest_steam = y;
            if (out_at < 0 && doused.alight() == 0) out_at = step + 1;
            if (doused.count("water") + doused.count("steam") + doused.count("ice") != water) {
                expect(false) << std::format("tick {}: water in all its phases is not what there was", step);
                break;
            }
        }
        dry.step(900);
        std::println("3: {} of water over 8 of burning wood: out after {} ticks with {} of wood left ({} without the water); at most {} of steam at once, as high as row {} (the wood is in row 16); {} of water at the end\n{}",
                     water, out_at, doused.count("wood"), dry.count("wood"), most_steam, highest_steam, doused.count("water"), doused.picture());
        expect(out_at > 0 && eq(doused.alight(), std::size_t{0})) << "the fire is out";
        expect(eq(doused.count("wood"), std::size_t{8})) << "with every piece of wood left";
        expect(eq(dry.count("wood"), std::size_t{0}) && eq(dry.count("ash"), std::size_t{8})) << "where the same fire without water burnt every piece";
        expect(most_steam >= 3u) << "water became steam";
        expect(highest_steam <= 13u) << "which rose from the wood, three rows and more";
        expect(eq(doused.count("steam"), std::size_t{0}) && eq(doused.count("water"), water)) << "and is water again, all of it";
    };

    "4 — lava meets water: the water boils and steams, the steam condenses where the air is still cool, the lava sets as stone"_test = [] {
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

    "5 — ice by a fire melts, the nearest first; water in the cold freezes"_test = [] {
        // A room at 250 K: ice keeps. A fire at one end of a row of it.
        World room(standard(), {14, 6}, "air");
        room.fill(0, 5, 13, 5, "stone");
        room.fill(5, 4, 12, 4, "ice");
        room.put(1, 4, "wood");
        room.put(2, 4, "wood");
        room.room(250);
        room.heat(1, 4, 700);
        room.heat(2, 4, 700);
        std::array<int, 8> melted;
        melted.fill(-1);
        // Where each piece of ice was when it melted: ice is solid and stays; its water runs.
        for (int step = 0; step < 500; ++step) {
            room.step();
            for (std::uint32_t piece = 0; piece < 8; ++piece)
                if (melted[piece] < 0 && !room.is(5 + piece, 4, "ice")) melted[piece] = step + 1;
            if (melted[0] > 0 && step == melted[0] + 1) expect(room.count("ice") >= 6u) << "when the nearest has melted the far ones are ice still";
        }
        std::println("5: a row of 8 of ice beside a fire in a room at 250 K melted at ticks {} (-1: still ice after 500)", melted);
        expect(melted[0] > 0) << "the piece nearest the fire melted";
        for (std::size_t piece = 1; piece < 8; ++piece)
            if (melted[piece] > 0) expect(melted[piece] >= melted[piece - 1] && melted[piece - 1] > 0) << std::format("piece {} at {}, the nearer one at {}", piece, melted[piece], melted[piece - 1]);
        // The same room with no fire: the ice keeps, and water let into it freezes.
        World cold(standard(), {8, 5}, "air");
        cold.fill(0, 4, 7, 4, "stone");
        cold.fill(1, 3, 2, 3, "ice");
        cold.room(250);
        cold.fill(4, 3, 6, 3, "water");
        int frozen_at = -1;
        for (int step = 0; step < 400 && frozen_at < 0; ++step) {
            cold.step();
            if (cold.count("water") == 0) frozen_at = step + 1;
        }
        expect(frozen_at > 0 && eq(cold.count("ice"), std::size_t{5})) << "the water froze, and the ice that was there kept\n" << cold.picture();
        World warm(standard(), {8, 5}, "air");
        warm.fill(0, 4, 7, 4, "stone");
        warm.fill(4, 3, 6, 3, "water");
        warm.step(400);
        expect(eq(warm.count("water"), std::size_t{3})) << "and at 293 K it stays water";
        std::println("   water let into the room at 250 K was ice after {} ticks", frozen_at);
    };

    "6 — oil afloat catches and burns on the water: the flame goes over the slick from cell to cell"_test = [] {
        World world(standard(), {16, 10}, "air");
        world.fill(0, 9, 15, 9, "stone");
        world.fill(0, 1, 0, 8, "stone");
        world.fill(15, 1, 15, 8, "stone");
        world.fill(1, 3, 14, 6, "water");
        world.fill(1, 7, 14, 8, "oil");  // let go under the water: two rows of it, the basin's whole width
        const std::size_t water = 56, oil = 28;
        world.step(300);
        expect(eq(world.count("oil"), oil) && world.depth("oil") < world.depth("water")) << "the oil came up and lies on the water\n" << world.picture();
        for (std::uint32_t x = 1; x <= 14; ++x) expect(world.is(x, 8, "water")) << "water at the bottom now";
        // A flame to the oil at one end of the slick.
        std::uint32_t lit_x = 99, lit_y = 0;
        for (std::uint32_t y = 0; y < 9 && lit_x == 99; ++y)
            for (std::uint32_t x = 0; x < 16 && lit_x == 99; ++x)
                if (world.is(x, y, "oil")) lit_x = x, lit_y = y;
        expect(fatal(lit_x != 99));
        world.heat(lit_x, lit_y, 700);
        std::size_t most_alight = 0, on_water = 0, alight_ticks = 0;
        int burnt_out = -1;
        for (int step = 0; step < 4000 && burnt_out < 0; ++step) {
            world.step();
            most_alight = std::max(most_alight, world.alight());
            // Alight with water under it — straight under it, or under the steam it has made of the water's top.
            for (std::uint32_t y = 0; y < 8; ++y)
                for (std::uint32_t x = 0; x < 16; ++x) {
                    if (!world.is(x, y, "oil") || !world.value("burning", x, y)) continue;
                    ++alight_ticks;
                    std::uint32_t under = y + 1;
                    while (under < 9 && (world.is(x, under, "steam") || world.is(x, under, "oil") || world.is(x, under, "smoke"))) ++under;
                    on_water += world.is(x, under, "water");
                }
            if (world.count("oil") == 0) burnt_out = step + 1;
        }
        std::println("6: 28 of oil on water, lit at one end: at most {} alight at once, all smoke after {} ticks; {} of {} cell-ticks alight had water under them; under a fire that long the water boils — {} of {} are steam at the end\n{}",
                     most_alight, burnt_out, on_water, alight_ticks, world.count("steam"), water, world.picture());
        expect(burnt_out > 0 && eq(world.count("smoke"), oil)) << "every cell of oil burnt to smoke";
        expect(most_alight >= 3u) << "the flame went from cell to cell over the slick";
        expect(on_water * 2 > alight_ticks && on_water > 500u) << std::format("burning where it floated: {} of {} cell-ticks alight had water under them", on_water, alight_ticks);
        expect(eq(world.count("water") + world.count("steam") + world.count("ice"), water)) << "and the water, in its phases, is what there was";
    };

    "7 — metal carries heat: wood at the far end of an iron bar catches, of a stone bar much later or not at all; charge goes through iron and water and stops at wood"_test = [] {
        // A bar of six on a stone floor in a closed room, its near end kept at 1250 K (the scene's input, written
        // every tick). Closed: `thermal` runs from 1 (air) to 8 (iron), where iron is thousands of times air — in the
        // open a bare bar of six gives its heat to the air before its far end is warm (the wood never catches).
        const auto caught_through = [](std::string_view bar) {
            World world(standard(), {12, 6}, "air");
            world.closed = true;
            world.fill(0, 5, 11, 5, "stone");
            world.fill(2, 4, 7, 4, bar);
            world.put(8, 4, "wood");
            for (int step = 0; step < 2500; ++step) {
                world.heat(2, 4, 1250);
                world.step();
                if (world.value("burning", 8, 4)) return step + 1;
            }
            return -1;
        };
        const int through_iron = caught_through("iron"), through_stone = caught_through("stone");
        std::println("7: wood at the far end of a bar of six from a 1250 K source caught at tick {} through iron, {} through stone (-1: not in 2500)", through_iron, through_stone);
        expect(through_iron > 0) << "the wood at the end of the iron bar caught";
        expect(through_stone < 0 || through_stone > 2 * through_iron) << "through stone much later, or not at all";
        // Charge put on one end of a wire that goes into water, with wood after the water.
        World wire(standard(), {10, 4}, "air");
        wire.fill(0, 3, 9, 3, "stone");
        wire.fill(1, 2, 4, 2, "iron");
        wire.fill(5, 1, 5, 2, "stone");  // the water's cup is the wire's end and a wall
        wire.fill(8, 1, 8, 2, "wood");
        wire.fill(5, 2, 5, 2, "iron");
        wire.fill(6, 2, 7, 2, "water");
        wire.value("charge", 1, 2) = 1'000'000;
        wire.step(300);
        std::uint64_t in_water = 0;
        for (std::uint32_t y = 0; y < 3; ++y)
            for (std::uint32_t x = 0; x < 10; ++x)
                if (wire.is(x, y, "water")) in_water += wire.value("charge", x, y);
        expect(wire.value("charge", 4, 2) > 0u && in_water > 0u) << "along the iron and into the water";
        expect(eq(wire.value("charge", 8, 2), 0u) && eq(wire.value("charge", 8, 1), 0u)) << "and not into the wood";
        std::uint64_t total = 0;
        for (std::size_t cell = 0; cell < wire.area(); ++cell) total += wire.cells[*wire.program.channel("charge") * wire.area() + cell];
        expect(eq(total, std::uint64_t{1'000'000})) << "all of it still there";
    };

    "8 — gunpowder goes off: a trail of it is gone in ticks where wood takes hundreds, and its pressure pushes a body away"_test = [] {
        const matter::Program program = standard();
        // A trail on a stone floor, lit at its far end, and a body of rubber standing two cells from its near end.
        // In the other world the trail is wood: the same flame, no blast.
        const auto run = [&](std::string_view trail) {
            World world(program, {40, 12}, "air");
            world.fill(0, 11, 39, 11, "stone");
            world.fill(4, 10, 13, 10, trail);
            // The body: a ring of 16, 3 cells across, in units of a tenth of a cell; the floor's top is its ground.
            const double cell = 10;
            std::vector<std::array<double, 2>> outline;
            for (int index = 0; index < 16; ++index) {
                const double angle = 2 * std::numbers::pi * index / 16;
                outline.push_back({17.5 * cell + 15 * std::cos(angle), 9.5 * cell + 15 * std::sin(angle)});
            }
            const std::array ground{dyn::Plane{.normal_x = 0, .normal_y = -1, .offset = -11 * cell}};
            auto body = dyn::SoftBody::ring(outline, dyn::params_of(program, *program.material("rubber"), 7), ground);
            expect(fatal(body.has_value()));
            for (int settle = 0; settle < 240; ++settle) dyn::SoftBodyModel::step(*body, {});
            const double stood_at = body->centre()[0];
            world.heat(4, 10, 700);
            std::int64_t hardest = 0;
            std::uint32_t most_pressure = 0;
            int gone_at = -1;
            const std::size_t pressure = *program.channel("pressure") * world.area();
            for (int tick = 0; tick < 400; ++tick) {
                world.step();
                if (gone_at < 0 && world.count(trail) == 0) gone_at = tick + 1;
                // What the field's pressure does to the cells the body is in, taken as a push for this tick's steps.
                const auto [left, right] = std::ranges::minmax(body->x);
                const auto [top, bottom] = std::ranges::minmax(body->y);
                const ca::Tile region{static_cast<std::uint32_t>(std::clamp(left / cell, 0.0, 39.0)), static_cast<std::uint32_t>(std::clamp(top / cell, 0.0, 11.0)),
                                      static_cast<std::uint32_t>(std::max(1.0, std::ceil((right - left) / cell))), static_cast<std::uint32_t>(std::max(1.0, std::ceil((bottom - top) / cell)))};
                const ca::Push push = ca::push_on(world.shape, std::span(world.cells).subspan(pressure, world.area()), region);
                hardest = std::max(hardest, push.x);
                most_pressure = std::max(most_pressure, world.value("pressure", 16, 10));
                dyn::BodyInput input;
                input.push_x = static_cast<double>(push.x);
                input.push_y = static_cast<double>(push.y);
                for (int substep = 0; substep < 2; ++substep) dyn::SoftBodyModel::step(*body, input);
            }
            return std::tuple(gone_at, body->centre()[0] - stood_at, hardest, most_pressure, world.picture());
        };
        const auto [powder_gone, powder_moved, powder_push, powder_pressure, powder_picture] = run("gunpowder");
        const auto [wood_gone, wood_moved, wood_push, wood_pressure, wood_picture] = run("wood");
        std::println("8: a trail of 10 of gunpowder was gone after {} ticks (wood: {}; -1: not in 400); the pressure two cells off reached {}, its hardest push on the body {}; the body went {:.1f} "
                     "cells (beside the wood: {:.2f})\n{}",
                     powder_gone, wood_gone, powder_pressure, powder_push, powder_moved / 10, wood_moved / 10, powder_picture);
        expect(powder_gone > 0 && powder_gone < 60) << "the trail went off";
        expect(wood_gone < 0) << "where wood is still burning after 400";
        expect(powder_pressure > 0u && eq(wood_pressure, 0u)) << "a blast is pressure; a wood fire has none";
        expect(powder_moved - wood_moved > 20.0) << "the body was pushed away from it, by more than two cells";
        expect(std::abs(wood_moved) < 5.0) << "and beside the wood it stood where it was, within half a cell of its own settling";
    };

    "12 — a material that only says its properties has all of a liquid that burns: alcohol, two rows and no law — it lies on water, boils, its vapour rises and burns"_test = [] {
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

    "a fire is as hot as its flame: wood and oil burn out, the smoke rises, the stone under them stays stone; sealed from air nothing burns"_test = [] {
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

    "the measure: materials added without a law, and what holds for each"_test = [] {
        const matter::Program program = standard();
        expect(eq(program.recipes(), std::size_t{0})) << "no law of the standard definition names a material";
        std::size_t transfers = 0;
        for (const matter::Law& law : program.laws()) transfers += law.kind == matter::LawKind::transfer;
        std::println("the standard definition: {} materials, {} laws ({} of them flows), {} naming a material; the set above is 9 phenomena of these laws and no other", program.materials().size(),
                     program.laws().size(), transfers, program.recipes());
        // What a law reads is a column or a channel: no law's text has a material's name in it (the kernel
        // refuses one; this reads what was prepared).
        for (const matter::Law& law : program.laws())
            for (const matter::Material& material : program.materials()) {
                const std::string text = " " + law.text + " ";
                for (std::size_t at = text.find(material.name); at != std::string::npos; at = text.find(material.name, at + 1)) {
                    const auto word = [](char letter) { return std::isalnum(static_cast<unsigned char>(letter)) || letter == '_'; };
                    expect(word(text[at - 1]) || word(text[at + material.name.size()])) << std::format("law `{}` names `{}`", law.name, material.name);
                }
            }
    };
}};
