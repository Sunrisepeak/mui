// xengine.matter on small sets of instances: a definition prepared, the reference step's arithmetic,
// and what the standard laws (standard.matter.omui.ts) make of materials that only say their
// properties — fire spreading, water boiling and freezing, lava meeting water, iron carrying heat.
// None of the laws names a material; the last cases add one and change no law.
import std;
import xengine.matter;
import mlibs.error;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;

namespace {

std::string standard_text() {
    const auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "standard.matter.omui.json";
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
matter::Definition standard_definition() {
    auto read = matter::read(standard_text(), "standard.matter.omui.json");
    expect(fatal(read.has_value())) << (read ? "" : read.error().front().text());
    return *read;
}
matter::Program prepared(const matter::Definition& definition) {
    auto program = matter::Program::prepare(definition);
    expect(fatal(program.has_value())) << (program ? "" : program.error().where + ": " + program.error().message);
    return *program;
}
matter::Program standard() { return prepared(standard_definition()); }

// Instances in a row, each beside the next.
std::vector<matter::Pair> row(std::uint32_t count) {
    std::vector<matter::Pair> pairs;
    for (std::uint32_t index = 0; index + 1 < count; ++index) pairs.push_back({index, index + 1, 1});
    return pairs;
}
// A grid, each beside the four next to it: instance y × width + x.
std::vector<matter::Pair> grid(std::uint32_t width, std::uint32_t height) {
    std::vector<matter::Pair> pairs;
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            if (x + 1 < width) pairs.push_back({y * width + x, y * width + x + 1, 1});
            if (y + 1 < height) pairs.push_back({y * width + x, (y + 1) * width + x, 1});
        }
    return pairs;
}

// A world of instances stepped by a program, read by channel and material name.
struct World {
    matter::Program            program;
    std::size_t                instances;
    std::vector<matter::Pair>  pairs;
    std::vector<std::uint32_t> state, next;
    matter::Workspace          workspace;
    std::vector<matter::Event> events;
    matter::Report             last;
    std::int64_t               steps = 0;

    World(matter::Program of, std::size_t count, std::vector<matter::Pair> beside, std::string_view filled)
        : program(std::move(of)), instances(count), pairs(std::move(beside)), state(program.fresh(count, *program.material(filled))), next(state.size()) {}
    void put(std::size_t instance, std::string_view material) { program.place(state, instances, instance, *program.material(material)); }
    [[nodiscard]] std::uint32_t& at(std::string_view channel, std::size_t instance) { return state[*program.channel(channel) * instances + instance]; }
    [[nodiscard]] std::string_view material(std::size_t instance) const { return program.materials()[state[instance]].name; }
    // Temperature in kelvin: the channel holds 256ths.
    [[nodiscard]] double kelvin(std::size_t instance) { return at("temperature", instance) / 256.0; }
    void heat(std::size_t instance, std::uint32_t kelvins) { at("temperature", instance) = kelvins * 256; }
    [[nodiscard]] std::uint64_t total(std::string_view channel) const {
        const std::size_t index = *program.channel(channel);
        return std::accumulate(state.begin() + static_cast<std::ptrdiff_t>(index * instances), state.begin() + static_cast<std::ptrdiff_t>((index + 1) * instances), std::uint64_t{0});
    }
    void step(int count = 1) {
        for (int index = 0; index < count; ++index, ++steps) {
            const auto stepped = program.step({instances, pairs}, state, next, workspace, &events);
            expect(fatal(stepped.has_value())) << (stepped ? "" : stepped.error().where + ": " + stepped.error().message);
            last = *stepped;
            state.swap(next);
        }
    }
    // How often an event of that name happened, and to which instance first.
    [[nodiscard]] std::size_t happened(std::string_view event) const {
        const auto names = program.event_names();
        return static_cast<std::size_t>(std::ranges::count_if(events, [&](const matter::Event& each) { return each.name < names.size() && names[each.name] == event; }));
    }
    [[nodiscard]] bool happened_to(std::string_view event, std::size_t instance) const {
        const auto names = program.event_names();
        return std::ranges::any_of(events, [&](const matter::Event& each) { return each.instance == instance && each.name < names.size() && names[each.name] == event; });
    }
    [[nodiscard]] std::string picture(std::uint32_t width) const {
        std::string out;
        for (std::size_t instance = 0; instance < instances; ++instance) {
            out += material(instance).substr(0, 2);
            out += (instance + 1) % width == 0 ? '\n' : ' ';
        }
        return out;
    }
};

// A strip of fuel on stone under air, `width` long: row 0 air, row 1 the fuel, row 2 stone.
World strip(matter::Program program, std::uint32_t width, std::string_view fuel = "wood", std::string_view above = "air") {
    World world(std::move(program), width * 3, grid(width, 3), "stone");
    for (std::uint32_t x = 0; x < width; ++x) {
        world.put(x, above);
        world.put(width + x, fuel);
    }
    return world;
}

using P = matter::Property;

// What a refused definition says: its code, where, and its message.
std::string refusal(const matter::Definition& definition, matter::Limits limits = {}) {
    const auto program = matter::Program::prepare(definition, limits);
    return program ? std::string("accepted") : std::format("{} at {}: {}", program.error().description().name, program.error().where, program.error().message);
}

}  // namespace

UTest cases{"xengine.matter", [] {
    "the standard definition is prepared: properties by column, as written and as the laws read them; laws by kind; what it holds said as JSON"_test = [] {
        const matter::Program plan = standard();
        const auto wood = *plan.material("wood");
        expect(eq(plan.value(wood, *plan.column("ignition")), 570u * 256u) && eq(plan.number(wood, *plan.column("ignition")), 570.0) && eq(plan.number(wood, *plan.column("friction")), 0.3))
            << "kelvin as written, 256ths as the laws read it";
        expect(eq(plan.value(wood, *plan.column("friction")), 19661u)) << "0.3 as 16 bits after the point";
        expect(eq(plan.value(*plan.material("air"), *plan.column("density")), 19u) && eq(plan.value(*plan.material("steam"), *plan.column("density")), 10u))
            << "a gas lighter than another stays so as whole numbers (16ths)";
        expect(eq(plan.value(wood, *plan.column("burnt")), *plan.material("ash")) && eq(plan.value(wood, *plan.column("boiled")), matter::none));
        expect(plan.phase(*plan.material("water")) == matter::Phase::liquid && plan.phase(wood) == matter::Phase::solid);
        expect(eq(plan.initial(*plan.material("lava"), *plan.channel("temperature")), 1600u * 256u) && eq(plan.initial(wood, *plan.channel("temperature")), 293u * 256u))
            << "a new instance: its material's own start, or the channel's";
        expect(eq(plan.initial(*plan.material("water"), *plan.channel("wetness")), 64u));
        const auto kinds = [&](matter::LawKind kind) { return std::ranges::count_if(plan.laws(), [&](const matter::Law& law) { return law.kind == kind; }); };
        expect(eq(plan.laws().size(), std::size_t{13}) && kinds(matter::LawKind::transfer) == 3 && kinds(matter::LawKind::adjust) == 1 && kinds(matter::LawKind::reaction) == 4 &&
               kinds(matter::LawKind::conversion) == 5);
        expect(eq(plan.recipes(), std::size_t{0})) << "no law names a material";
        const std::string said = plan.describe();
        expect(mlibs::json::Document::parse(said, "describe").has_value()) << "it is JSON";
        expect(said.contains(R"({"name":"wood","id":4,"tags":["plant"],"density":600,"ignition":570,"fuel":400,"heat":80,"porous":2,"burnt":"ash"})")) << said;
        expect(said.contains(R"({"name":"burns","kind":"reaction","when":"temperature >= ignition && burned < fuel && near.oxygen > 0","changes":["burned","temperature"],"flag":"burning"})")) << said;
        expect(said.contains(R"({"name":"heat flows","kind":"transfer","transfer":"temperature","share":"1 / 64","by":"thermal"})")) << said;
        expect(said.contains(R"({"name":"boils","kind":"conversion","when":"phase == liquid && boiling > 0 && temperature >= boiling","become":"boiled","event":"boil"})")) << said;
    };

    "heat flows along a row and what there is of it stays: iron carries it further than stone in the same time"_test = [] {
        const auto reach = [](std::string_view material) {
            World world(standard(), 12, row(12), material);
            world.heat(0, 1200);
            const std::uint64_t before = world.total("temperature");
            for (int step = 0; step < 200; ++step) {
                world.step();
                expect(eq(world.total("temperature"), before)) << std::format("{} at step {}", material, step);
            }
            return world.kelvin(6);
        };
        const double through_iron = reach("iron"), through_stone = reach("stone");
        expect(through_iron > through_stone && through_stone >= 293.0) << std::format("{} K through iron, {} K through stone", through_iron, through_stone);
        std::println("heat 200 steps along a row from 1200 K: the seventh instance at {:.1f} K in iron, {:.1f} K in stone", through_iron, through_stone);

        // What is not a material of the definition takes no part: nothing flows to it or from it, and it is as it was.
        World gap(standard(), 3, row(3), "iron");
        gap.state[1] = 4000;  // no such material
        gap.heat(0, 1200);
        gap.at("temperature", 1) = 5;
        gap.step(50);
        expect(eq(gap.kelvin(0), 1200.0) && eq(gap.at("temperature", 1), 5u) && eq(gap.kelvin(2), 293.0) && eq(gap.state[1], 4000u));
    };

    "wood lit at one end burns along the strip: each piece catches from the last, burns out to ash and goes out; without air beside it nothing burns"_test = [] {
        World world = strip(standard(), 8);
        world.heat(8, 700);  // the first piece, lit
        std::array<std::int64_t, 8> caught{};
        caught.fill(-1);
        for (int step = 0; step < 4000 && world.happened("burn_out") < 8; ++step) {
            world.step();
            for (std::size_t piece = 0; piece < 8; ++piece)
                if (caught[piece] < 0 && world.at("burning", 8 + piece)) caught[piece] = world.steps;
        }
        expect(fatal(eq(world.happened("burn_out"), std::size_t{8}))) << std::format("after {} steps:\n{}", world.steps, world.picture(8));
        expect(eq(world.happened("ignite"), std::size_t{8}) && eq(world.happened("go_out"), std::size_t{8})) << "each caught once and went out once";
        for (std::size_t piece = 0; piece < 8; ++piece) {
            expect(world.material(8 + piece) == "ash") << world.picture(8);
            if (piece) expect(caught[piece] > caught[piece - 1]) << std::format("piece {} caught at step {}, the one before at {}", piece, caught[piece], caught[piece - 1]);
        }
        std::println("a strip of 8 of wood lit at one end: the pieces caught at steps {}, all ash after {}", caught, world.steps);

        World sealed = strip(standard(), 8, "wood", "stone");  // stone above as below: no oxygen beside the wood
        sealed.heat(8, 700);
        sealed.step(600);
        expect(eq(sealed.happened("ignite"), std::size_t{0}) && sealed.material(8) == "wood") << "hot, with fuel, and nothing to burn in";
    };

    "wet wood is harder to light, and wood beside water becomes wet: by wetness raising the ignition point, no law about water and wood"_test = [] {
        const auto lit = [](std::uint32_t wetness, std::uint32_t temperature) {
            World world = strip(standard(), 1);
            world.at("wetness", 1) = wetness;
            world.heat(1, temperature);
            world.step();
            return world.at("burning", 1) != 0;
        };
        expect(lit(0, 600) && !lit(10, 600)) << "600 K lights dry wood (570) and not wood of wetness 10 (570 + 400)";
        expect(lit(10, 980)) << "hotter, it lights";

        // Water, then wood: the wetness seeps, the wood nearest the water wettest.
        World world(standard(), 5, row(5), "wood");
        world.put(0, "water");
        world.step(400);
        expect(world.at("wetness", 1) > world.at("wetness", 2) && world.at("wetness", 2) > 0u && eq(world.at("wetness", 0), 64u))
            << std::format("wetness along the row: {} {} {} {} {}", world.at("wetness", 0), world.at("wetness", 1), world.at("wetness", 2), world.at("wetness", 3), world.at("wetness", 4));
        // And in the heat it dries.
        World hot(standard(), 1, {}, "sand");
        hot.at("wetness", 0) = 5;
        hot.heat(0, 400);
        hot.step(5);
        expect(eq(hot.at("wetness", 0), 0u));
    };

    "water boils to steam, steam condenses, water freezes, ice melts: each material says what it becomes, the laws only when"_test = [] {
        World world(standard(), 4, {}, "water");
        world.heat(0, 400);
        world.heat(1, 250);
        world.put(2, "steam");
        world.heat(2, 300);
        world.put(3, "ice");
        world.heat(3, 300);
        world.step();
        expect(world.material(0) == "steam" && world.material(1) == "ice" && world.material(2) == "water" && world.material(3) == "water") << world.picture(4);
        expect(world.happened_to("boil", 0) && world.happened_to("freeze", 1) && world.happened_to("condense", 2) && world.happened_to("melt", 3));
        expect(eq(world.kelvin(0), 400.0)) << "it is as hot as it was";
        expect(eq(world.last.became, std::uint64_t{4}));
        world.events.clear();
        world.step(3);
        expect(world.events.empty()) << "and stays what it became";
    };

    "lava beside water: the water boils away as steam and the lava sets as stone — heat, a boiling point and a melting point, nothing about lava and water"_test = [] {
        World world(standard(), 2, row(2), "lava");
        world.put(1, "water");
        expect(eq(world.kelvin(0), 1600.0) && eq(world.kelvin(1), 293.0));
        world.step(60);
        expect(world.material(0) == "stone" && world.material(1) == "steam") << world.picture(2);
        expect(world.happened_to("freeze", 0) && world.happened_to("boil", 1));
        const auto when = [&](std::string_view event) {
            const auto names = world.program.event_names();
            const auto found = std::ranges::find_if(world.events, [&](const matter::Event& each) { return each.name < names.size() && names[each.name] == event; });
            return found - world.events.begin();
        };
        expect(when("boil") < when("freeze")) << "the water boils first";
        std::println("lava beside water after 60 steps: {} at {:.0f} K, {} at {:.0f} K", world.material(0), world.kelvin(0), world.material(1), world.kelvin(1));
    };

    "iron carries a fire's heat to wood at its far end, stone in the same time does not; charge goes through iron and water and stops at wood"_test = [] {
        // heat source, a bar of five, wood with air beside it: 0 the source, 1–5 the bar, 6 wood, 7 air beside the wood
        const auto far_wood = [](std::string_view bar) {
            World world(standard(), 8, {{0, 1, 1}, {1, 2, 1}, {2, 3, 1}, {3, 4, 1}, {4, 5, 1}, {5, 6, 1}, {6, 7, 1}}, bar);
            world.put(6, "wood");
            world.put(7, "air");
            for (int step = 0; step < 1500 && !world.happened("ignite"); ++step) {
                world.heat(0, 1250);  // kept hot from outside, below what melts stone
                world.step();
            }
            return world.happened("ignite") ? world.steps : std::int64_t{-1};
        };
        const std::int64_t through_iron = far_wood("iron"), through_stone = far_wood("stone");
        expect(through_iron > 0) << "the wood at the end of the iron bar caught";
        expect(through_stone < 0 || through_stone > through_iron * 2) << std::format("through iron at step {}, through stone at {}", through_iron, through_stone);
        std::println("wood at the far end of a bar of five from a 1250 K source: caught at step {} through iron, {} through stone (-1: not in 1500)", through_iron, through_stone);

        World wire(standard(), 6, row(6), "iron");
        wire.put(2, "water");
        wire.put(4, "wood");
        wire.at("charge", 0) = 64000;
        const std::uint64_t before = wire.total("charge");
        wire.step(500);
        expect(eq(wire.total("charge"), before)) << "charge is kept";
        expect(wire.at("charge", 3) > 0u) << "through the water to the iron beyond";
        expect(eq(wire.at("charge", 4), 0u) && eq(wire.at("charge", 5), 0u)) << "and not into the wood, nor past it";
    };

    "a material that only says its properties takes part: alcohol and its vapour are added, no law is changed — it boils, its vapour burns, beside a fire it is alight long before wood"_test = [] {
        matter::Definition definition = standard_definition();
        const std::size_t laws_before = definition.laws.size();
        definition.materials.push_back({"alcohol_vapour",
                                        {P{"phase", "gas"}, P{"density", 1.6}, P{"thermal", 1.0}, P{"boiling", 351.0}, P{"condensed", "alcohol"}, P{"starts_at", 360.0}, P{"ignition", 400.0},
                                         P{"fuel", 40.0}, P{"heat", 90.0}, P{"burnt", "smoke"}},
                                        {}});
        definition.materials.push_back({"alcohol", {P{"phase", "liquid"}, P{"density", 790.0}, P{"thermal", 1.0}, P{"boiling", 351.0}, P{"boiled", "alcohol_vapour"}}, {}});
        const matter::Program plan = prepared(definition);
        expect(eq(plan.laws().size(), laws_before) && eq(plan.recipes(), std::size_t{0}));
        std::size_t interactions = 0;

        // 1, 2 It boils at its own boiling point to its own vapour, and the vapour condenses back.
        World pot(plan, 2, {}, "alcohol");
        pot.heat(0, 360);
        pot.put(1, "alcohol_vapour");
        pot.heat(1, 300);
        pot.step();
        expect(pot.material(0) == "alcohol_vapour" && pot.material(1) == "alcohol") << pot.picture(2);
        interactions += pot.material(0) == "alcohol_vapour";
        interactions += pot.material(1) == "alcohol";

        // 3, 4, 5 Beside a burning piece of wood, with air above: it boils, the vapour catches — sooner than a
        // piece of wood in its place does — and burns out to smoke.
        const auto beside_fire = [&](std::string_view fuel) {
            World world(plan, 6, grid(3, 2), "air");  // row 0 air; row 1: burning wood, the fuel, stone
            world.put(3, "wood");
            world.put(4, fuel);
            world.put(5, "stone");
            world.heat(3, 700);
            std::int64_t caught = -1;
            for (int step = 0; step < 600; ++step) {
                world.step();
                if (caught < 0 && world.at("burning", 4)) caught = world.steps;
            }
            return std::tuple(caught, world.happened_to("boil", 4), std::string(world.material(4)));
        };
        const auto [wood_at, wood_boiled, wood_left] = beside_fire("wood");
        const auto [alcohol_at, alcohol_boiled, alcohol_left] = beside_fire("alcohol");
        std::println("beside a burning piece of wood: wood catches at step {} and is {} after 600; alcohol boils, its vapour catches at step {} and is {}", wood_at, wood_left, alcohol_at,
                     alcohol_left);
        expect(alcohol_boiled && !wood_boiled) << "it boiled first: 351 K";
        expect(alcohol_at > 0 && wood_at > 0 && alcohol_at < wood_at) << "and its vapour was alight before wood would have been";
        expect(alcohol_left == "smoke" && wood_left == "ash") << "each burnt out to what it says it leaves";
        interactions += alcohol_boiled;
        interactions += alcohol_at > 0 && alcohol_at < wood_at;
        interactions += alcohol_left == "smoke";

        // 6 It conducts heat like anything with `thermal`: 47 K of difference flows, in 256ths.
        World line(plan, 3, row(3), "alcohol");
        line.heat(0, 340);
        line.step(50);
        expect(line.kelvin(2) > 293.0 && line.material(0) == "alcohol");
        interactions += line.kelvin(2) > 293.0;
        expect(eq(interactions, std::size_t{6})) << "interactions for two rows of properties";
        std::println("alcohol and its vapour: 2 rows of properties, 0 laws added, {} interactions that hold", interactions);
    };

    "the same state whatever the order of the pairs and whichever end is written first; a step run again is the same step"_test = [] {
        const auto run = [](int order) {
            World world = strip(standard(), 6);
            world.put(12 + 2, "water");  // a piece of the floor is water
            world.heat(6, 700);
            if (order == 1) std::ranges::reverse(world.pairs);
            if (order == 2)
                for (matter::Pair& pair : world.pairs) std::swap(pair.first, pair.second);
            if (order == 3) {
                std::mt19937 shuffle(11);
                std::ranges::shuffle(world.pairs, shuffle);
            }
            world.step(300);
            return world.state;
        };
        const auto first = run(0);
        expect(run(0) == first) << "run again";
        expect(run(1) == first && run(2) == first && run(3) == first) << "the pairs are a set: their order and their ends are no part of the result";
    };

    "the reference step's arithmetic, by hand: the first set, then what is added, then what is taken, then what flowed; saturating at both ends; a flag; a start"_test = [] {
        matter::Definition definition = matter::Definition::standard();
        definition.columns.push_back({.name = "gain", .most = 4294967295.0});
        definition.channels.push_back({.name = "level", .initial = 7});
        definition.channels.push_back({.name = "mark"});
        definition.materials = {{"thing", {P{"gain", 5.0}, P{"thermal", 4.0}}, {}}, {"other", {P{"gain", 4000000000.0}, P{"thermal", 8.0}}, {}}};
        definition.laws = {
            {.name = "flows", .transfer = "level", .by = "thermal", .multiply = 1, .shift = 5},
            {.name = "more gain when marked", .when = "mark > 0", .raise = {{"gain", "mark * 2"}}},
            {.name = "first set", .when = "level >= 100 && level < 1000", .set = {{"level", "10"}}},
            {.name = "second set, lost to the first", .when = "level >= 50 && level < 1000", .set = {{"level", "20"}}},
            {.name = "adds", .add = {{"level", "gain + near.gain / 2"}}},
            {.name = "takes", .when = "level > 3", .take = {{"level", "3 * 2"}}, .flag = "mark"},
        };
        const matter::Program plan = prepared(definition);
        // three in a row: thing(level 100, mark 1), thing(level 2), other(level 4294967290)
        World world(plan, 3, row(3), "thing");
        world.put(2, "other");
        world.at("level", 0) = 100;
        world.at("mark", 0) = 1;
        world.at("level", 1) = 2;
        world.at("level", 2) = 4294967290u;
        world.step();
        // 0: gain 5 raised by mark × 2 = 7; near.gain = 5 (its neighbour's, as written) → adds 7 + 5/2 = 9; set to 10 (the first set: 100 ≥ 100);
        //    takes 6 (100 > 3) → 10 + 9 − 6 = 13; flow with 1: |100 − 2| × min(4, 4) / 32 = 12 lost → 13 − 12 = 1. mark: the flag's law held → 1.
        expect(eq(world.at("level", 0), 1u) && eq(world.at("mark", 0), 1u));
        // 1: gain 5; near.gain = 5 + 4000000000 → adds 5 + 2000000002; no set; level 2 is not > 3: nothing taken, mark 0;
        //    flows: +12 from 0; with 2: |4294967290 − 2| × min(4, 8) saturates at 4294967295, / 32 = 134217727 gained.
        expect(eq(world.at("level", 1), 2u + 5u + 2000000002u + 12u + 134217727u) && eq(world.at("mark", 1), 0u));
        // 2: no set (its level is beyond both); gain 4000000000, near.gain 5 → adds 4000000002: 4294967290 + that stops at 4294967295;
        //    takes 6 → 4294967289; loses 134217727.
        expect(eq(world.at("level", 2), 4294967289u - 134217727u) && eq(world.at("mark", 2), 1u));
        expect(eq(world.last.held, std::uint64_t{7})) << "both sets, adds, takes at 0; adds at 1; adds, takes at 2";
        // A new instance starts from the channel's own value.
        expect(eq(plan.fresh(2, 0)[*plan.channel("level") * 2], 7u));
    };

    "a definition is refused where it is wrong: a law that names a material, a name that is not there, a law of two kinds, a condition not well formed"_test = [] {
        const auto with = [](auto change) {
            matter::Definition definition = standard_definition();
            change(definition);
            return definition;
        };
        expect(eq(refusal(standard_definition()), std::string("accepted")));
        // Laws read properties.
        const std::string named = refusal(with([](matter::Definition& d) { d.laws.push_back({.when = "material == wood && temperature >= 500", .add = {{"temperature", "5"}}}); }));
        expect(named.starts_with("matter.names-material at /laws/13/when: ") && named.contains("recipe")) << named;
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.when = "near.oxygen > water", .add = {{"temperature", "5"}}}); })).starts_with("matter.names-material at /laws/13/when: `water` is a material"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.when = "temperature >= 400", .become = "steam"}); })).starts_with("matter.names-material at /laws/13/become: `steam` is a material"));
        // A special case says so, and is counted.
        const auto special = matter::Program::prepare(with([](matter::Definition& d) {
            d.laws.push_back({.name = "rust", .when = "material == iron && wetness > 20", .add = {{"damage", "1"}}, .recipe = true});
        }));
        expect(special.has_value() && eq(special->recipes(), std::size_t{1}) && special->describe().contains(R"("recipe":true)"));
        // Names.
        expect(eq(refusal(with([](matter::Definition& d) { d.laws.push_back({.when = "temprature >= 400", .add = {{"temperature", "1"}}}); })),
                  std::string("matter.name at /laws/13/when: `temprature` is no channel, property or phase (did you mean `temperature`?) — at column 11 of `temprature >= 400`")));
        expect(refusal(with([](matter::Definition& d) { d.materials[0].properties.push_back(P{"densty", 3.0}); })).starts_with("matter.name at /materials/air/densty: material `air`: no property `densty` (did you mean `density`?)"));
        expect(refusal(with([](matter::Definition& d) { d.materials[1].properties.push_back(P{"melted", "larva"}); })).starts_with("matter.name at /materials/stone/melted: material `stone` says `melted` twice"));
        expect(refusal(with([](matter::Definition& d) { d.materials.push_back({"mud", {P{"frozen", "drymud"}}, {}}); })).starts_with("matter.name at /materials/mud/frozen: `frozen` of `mud` is a material's name"));
        expect(refusal(with([](matter::Definition& d) { d.materials.push_back({"gas", {}, {}}); })).starts_with("matter.name at /materials/gas: `gas` is already"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.add = {{"heat", "1"}}}); })).starts_with("matter.name at /laws/13/add/heat: `heat` is no channel"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.transfer = "temperature", .by = "phase"}); })).starts_with("matter.name at /laws/13/by: "));
        // Values.
        expect(refusal(with([](matter::Definition& d) { d.materials.push_back({"mud", {P{"thermal", 9.0}}, {}}); })).starts_with("matter.value at /materials/mud/thermal: `thermal` of `mud` is a number 0 … 8"));
        expect(refusal(with([](matter::Definition& d) { d.materials.push_back({"mud", {P{"phase", "plasma"}}, {}}); })).starts_with("matter.value at /materials/mud/phase: `phase` of `mud` is a phase"));
        // Laws.
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.transfer = "charge", .by = "conduct", .add = {{"temperature", "1"}}}); })).starts_with("matter.law at /laws/13: a law is one kind"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.when = "wetness > 0"}); })).starts_with("matter.law at /laws/13: a law does something"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.transfer = "temperature", .by = "conduct"}); })).starts_with("matter.law at /laws/13/transfer: `temperature` already flows by law 0"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.add = {{"burning", "1"}}}); })).starts_with("matter.law at /laws: `burning` is a flag"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.when = "near.oxygen > 0", .raise = {{"ignition", "5"}}}); })).starts_with("matter.law at /laws/13: an adjustment reads the instance alone"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.set = {{"material", "3"}}}); })).starts_with("matter.law at /laws/13/set/material: what a thing is changes by `become`"));
        // Conditions and amounts.
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.when = "temperature >= ", .add = {{"temperature", "1"}}}); })).starts_with("matter.expression at /laws/13/when: a name or a whole number is expected here"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.when = "temperature 400", .add = {{"temperature", "1"}}}); })).starts_with("matter.expression at /laws/13/when: a comparison is expected here"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.add = {{"temperature", "heat / 3"}}}); })).starts_with("matter.expression at /laws/13/add/temperature: a term is divided by a power of two"));
        expect(refusal(with([](matter::Definition& d) { d.laws.push_back({.add = {{"temperature", "heat > 3"}}}); })).starts_with("matter.expression at /laws/13/add/temperature: an amount is a sum of terms"));
        // Limits: refused as such.
        const auto over = matter::Program::prepare(standard_definition(), {.max_laws = 4});
        expect(!over && over.error().kind() == mlibs::error::Kind::refused && eq(over.error().where, std::string("/laws")));
    };

    "a step is refused before it writes: state of the wrong shape, input and output the same, a pair that is none, a transfer with more neighbours than its share allows"_test = [] {
        const matter::Program plan = standard();
        matter::Workspace workspace;
        std::vector<std::uint32_t> state = plan.fresh(12, *plan.material("iron")), next(state.size(), 77u);
        const auto refused = [&](matter::Carrier carrier, std::span<const std::uint32_t> input, std::span<std::uint32_t> output) {
            const auto stepped = plan.step(carrier, input, output, workspace);
            return stepped ? std::string("stepped") : std::string(stepped.error().description().name);
        };
        const std::vector<matter::Pair> beside = row(12);
        expect(eq(refused({11, beside}, state, next), std::string("matter.shape")));
        expect(eq(refused({12, beside}, state, state), std::string("matter.shape"))) << "the input is not the output";
        const std::vector<matter::Pair> none{{0, 12, 1}}, itself{{3, 3, 1}};
        expect(eq(refused({12, none}, state, next), std::string("matter.pair")) && eq(refused({12, itself}, state, next), std::string("matter.pair")));
        // Nine beside one: iron's 8/64 to each would be more than all of a difference.
        std::vector<matter::Pair> star;
        for (std::uint32_t leaf = 1; leaf <= 9; ++leaf) star.push_back({0, leaf, 1});
        const auto unstable = plan.step({12, star}, state, next, workspace);
        expect(!unstable && unstable.error().code == matter::Errc::unstable && eq(unstable.error().where, std::string("/laws/0")) && unstable.error().message.contains("instance 0 has 9 beside it"))
            << (unstable ? "" : unstable.error().message);
        expect(std::ranges::all_of(next, [](std::uint32_t value) { return value == 77u; })) << "nothing was written";
        star.pop_back();
        expect(eq(refused({12, star}, state, next), std::string("stepped"))) << "eight, it holds";
        expect(eq(refused({0, {}}, {}, {}), std::string("stepped"))) << "no instance: nothing to do";
        const matter::Program unprepared;
        const auto bare = unprepared.step({0, {}}, {}, {}, workspace);
        expect(!bare && bare.error().code == matter::Errc::unprepared);
    };

    "a definition is read from JSON, and its problems are said by pointer at their place in the text — reading's and preparing's alike"_test = [] {
        const std::string text = R"({"matter": {
  "columns": {"sticky": {"unit": "", "most": 10, "defualt": 1}},
  "channels": {"pressure": {"initial": 5, "resets": "yes"}},
  "materials": {"mud": {"phase": "liquid", "density": 1500, "sticky": 3, "tags": ["earth"]}, "clay": {"density": true}},
  "laws": [{"transfer": "pressure", "by": "sticky"}, {"when": "pressure > 3", "add": {"pressure": []}, "becomes": "frozen"}],
  "law": []
}})";
        const auto read = matter::read(text, "bad.matter.omui.json");
        expect(fatal(!read.has_value()));
        std::vector<std::string> said;
        for (const auto& problem : read.error()) said.push_back(problem.text());
        const auto has = [&](std::string_view line) { return std::ranges::find(said, line) != said.end(); };
        expect(has("bad.matter.omui.json:6:10: /matter/law: a matter definition has no key `law` (did you mean `laws`?)")) << said;
        expect(has("bad.matter.omui.json:2:61: /matter/columns/sticky/defualt: column `sticky` has no key `defualt` (did you mean `default`?)")) << said;
        expect(has("bad.matter.omui.json:3:53: /matter/channels/pressure/resets: `resets` is true or false")) << said;
        expect(has("bad.matter.omui.json:4:114: /matter/materials/clay/density: `density` of `clay` is a number, or the name of a phase or a material")) << said;
        expect(has("bad.matter.omui.json:5:99: /matter/laws/1/add/pressure: the amount `pressure` changes by is a sum as text (\"wetness * 40\") or a whole number")) << said;
        expect(has("bad.matter.omui.json:5:115: /matter/laws/1/becomes: a law has no key `becomes` (did you mean `become`?)")) << said;
        expect(eq(said.size(), std::size_t{6})) << said;

        // Read, and wrong only when prepared: the same place, the same form.
        const std::string late = R"({"matter": {
  "materials": {"mud": {"phase": "liquid", "frozen": "dry_mud"}},
  "laws": []
}})";
        const auto plan = matter::prepare_document(late, "late.matter.omui.json");
        expect(fatal(!plan.has_value()));
        expect(eq(plan.error().front().text(), std::string("late.matter.omui.json:2:54: /matter/materials/mud/frozen: `frozen` of `mud` is a material's name (one of `mud`)")));

        // What is written reads; on the standard columns and channels.
        const std::string good = R"({"matter": {
  "columns": {"sticky": {"unit": "", "most": 8, "default": 1}},
  "channels": {"pressure": {"initial": 5, "rise": "pressed"}},
  "materials": {"mud": {"phase": "liquid", "density": 1500, "sticky": 3, "tags": ["earth"]}},
  "laws": [{"transfer": "pressure", "by": "sticky"}, {"when": "pressure > 3", "take": {"pressure": 1}}]
}})";
        const auto made = matter::prepare_document(good, "good.matter.omui.json");
        expect(fatal(made.has_value())) << (made ? "" : made.error().front().text());
        expect(eq(made->value(*made->material("mud"), *made->column("sticky")), 3u) && eq(made->channels().back().initial, 5u) && eq(made->laws().size(), std::size_t{2}));
        expect(eq(made->channels().size(), matter::Definition::standard().channels.size() + 1));
    };
}};
