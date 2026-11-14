module xengine.matter.cts;

import std;
import mlibs.hash;
import mlibs.math;
import xengine.matter.json;

namespace xengine::matter::cts {
namespace {

using mlibs::cts::Check;
using P = Property;

constexpr std::string_view contract = "xengine.matter.carrier@1";

// A world for a case: a program's instances on a grid, written by name.
struct World {
    Program program;
    Grid grid;
    std::vector<std::uint32_t> state;

    World(Program of, Grid on, std::string_view filled) : program(std::move(of)), grid(on), state(program.fresh(cells(), *program.material(filled))) {}
    [[nodiscard]] std::size_t cells() const { return static_cast<std::size_t>(grid.width) * grid.height; }
    [[nodiscard]] std::size_t at(std::uint32_t x, std::uint32_t y) const { return static_cast<std::size_t>(y) * grid.width + x; }
    void put(std::uint32_t x, std::uint32_t y, std::string_view material) { program.place(state, cells(), at(x, y), *program.material(material)); }
    [[nodiscard]] std::uint32_t& value(std::string_view channel, std::uint32_t x, std::uint32_t y) { return state[*program.channel(channel) * cells() + at(x, y)]; }
    void heat(std::uint32_t x, std::uint32_t y, std::uint32_t kelvins) { value("temperature", x, y) = kelvins * 256; }
    void row(std::uint32_t y, std::string_view material) {
        for (std::uint32_t x = 0; x < grid.width; ++x) put(x, y, material);
    }
};

// What the reference made of a world, for a case to say that something happened in it.
struct Outcome {
    bool same = true;
    std::uint64_t held = 0, became = 0, changed = 0;
    std::vector<std::uint32_t> state;
    [[nodiscard]] std::size_t count(const Program& program, std::string_view material) const {
        const std::uint32_t id = *program.material(material);
        return static_cast<std::size_t>(std::ranges::count(std::span(state).first(state.size() / program.channels().size()), id));
    }
};

// The reference and the subject, each stepping its own state from the same start: the same bits after every step.
Outcome compare(Check& check, const Subject& subject, const World& world, int steps) {
    const Program& program = world.program;
    const std::size_t cells = world.cells();
    const std::vector<Pair> pairs = pairs_of(world.grid);
    Outcome outcome;
    Step carried;
    if (subject.carry) {
        carried = subject.carry(program, world.grid);
        if (!check.that(static_cast<bool>(carried), "the carrier could not be readied for the program")) return {.same = false};
    }
    std::vector<std::uint32_t> reference = world.state, reference_next(reference.size()), state = world.state, next(state.size());
    Workspace workspace;
    for (int step = 0; step < steps; ++step) {
        const auto stepped = program.step({cells, pairs}, reference, reference_next, workspace);
        if (!check.that(stepped.has_value(), stepped ? std::string() : std::format("the reference refused step {}: {}", step, stepped.error().message))) return {.same = false};
        outcome.held += stepped->held;
        outcome.became += stepped->became;
        outcome.changed += stepped->changed;
        reference.swap(reference_next);
        if (!carried) continue;
        if (!check.that(carried(state, next), std::format("the carrier could not take step {}", step))) return {.same = false};
        state.swap(next);
        if (state == reference) continue;
        const std::size_t first = static_cast<std::size_t>(std::ranges::mismatch(state, reference).in1 - state.begin());
        const std::size_t channel = first / cells, instance = first % cells;
        std::size_t apart = 0;
        for (std::size_t index = 0; index < state.size(); ++index) apart += state[index] != reference[index];
        check.that(false, std::format("step {}: `{}` of instance {} (x {}, y {}; {}) is {}, the reference's {} — {} values apart", step, program.channels()[channel].name, instance,
                                      instance % world.grid.width, instance / world.grid.width,
                                      reference[instance] < program.materials().size() ? std::string_view(program.materials()[reference[instance]].name) : std::string_view("no material"),
                                      state[first], reference[first], apart));
        outcome.same = false;
        break;
    }
    outcome.state = std::move(reference);
    mlibs::hash::Fnv1a hash;
    for (const std::uint32_t value : outcome.state) hash.u64(value);
    check.evidence("state", hash.hex());
    return outcome;
}

Program standard_program() { return *standard(); }

// The standard definition and two rows more: a material that only says its properties.
Program with_alcohol(Check& check) {
    auto definition = read(standard_snapshot, "standard.matter.omui.json");
    if (!check.that(definition.has_value(), "the standard definition reads")) return {};
    definition->materials.push_back({"alcohol_vapour",
                                     {P{"phase", "gas"}, P{"density", 1.6}, P{"thermal", 1.0}, P{"boiling", 351.0}, P{"condensed", "alcohol"}, P{"starts_at", 360.0}, P{"ignition", 400.0},
                                      P{"fuel", 40.0}, P{"heat", 90.0}, P{"burnt", "smoke"}},
                                     {}});
    definition->materials.push_back({"alcohol", {P{"phase", "liquid"}, P{"density", 790.0}, P{"thermal", 1.0}, P{"boiling", 351.0}, P{"boiled", "alcohol_vapour"}}, {}});
    auto program = Program::prepare(*definition);
    check.that(program.has_value(), program ? std::string() : std::format("{}: {}", program.error().where, program.error().message));
    return program ? *program : Program{};
}

// A definition of its own, for what the standard laws do not use: a sum over the neighbours' state, a set that
// a later set does not replace, a property lowered and raised, a flow by no property, a channel that starts
// again from its new material's own value, a flag, terms × a number and / a power of two.
Program garden(Check& check) {
    Definition definition = Definition::standard();
    definition.columns.push_back({"vigour", ColumnKind::number, "a step", "how fast it grows", 0, 0, 1000, 0});
    definition.columns.push_back({"seeded_at", ColumnKind::number, "", "the growth it starts with", 0, 0, 100, 0});
    definition.columns.push_back({"grown", ColumnKind::material, "material", "what it grows into", 0, 0, 0, static_cast<double>(none)});
    definition.columns.push_back({"wilted", ColumnKind::material, "material", "what heat leaves of it", 0, 0, 0, static_cast<double>(none)});
    definition.channels.push_back({.name = "growth", .from = "seeded_at", .resets = true});
    definition.channels.push_back({.name = "crowd"});
    definition.channels.push_back({.name = "thriving", .rise = "thrive", .fall = "flag"});
    definition.materials = {
        {"air", {P{"phase", "gas"}, P{"density", 1.2}, P{"thermal", 1.0}, P{"oxygen", 1.0}}, {}},
        {"soil", {P{"phase", "granular"}, P{"density", 1500.0}, P{"thermal", 2.0}, P{"moist", 10.0}, P{"porous", 4.0}}, {}},
        {"sprout", {P{"density", 300.0}, P{"thermal", 1.0}, P{"porous", 2.0}, P{"vigour", 3.0}, P{"seeded_at", 5.0}, P{"grown", "plant"}, P{"wilted", "soil"}}, {}},
        {"plant", {P{"density", 500.0}, P{"thermal", 1.0}, P{"porous", 3.0}, P{"vigour", 1.0}, P{"seeded_at", 2.0}, P{"wilted", "soil"}}, {}},
        {"rock", {P{"density", 2500.0}, P{"thermal", 3.0}, P{"warmth", 1.0}}, {}},
    };
    definition.laws = {
        {.name = "heat flows", .transfer = "temperature", .by = "thermal"},
        {.name = "water seeps", .transfer = "wetness", .by = "porous"},
        {.name = "charge spreads", .transfer = "charge", .shift = 3},
        {.name = "heat tires", .when = "temperature > 320", .lower = {{"vigour", "2"}}},
        {.name = "water feeds", .when = "wetness > 3", .raise = {{"vigour", "wetness / 2"}}},
        {.name = "holds its water", .when = "moist > 0", .set = {{"wetness", "moist"}}},
        {.name = "gives off heat", .when = "warmth > 0", .add = {{"temperature", "warmth"}}},
        {.name = "grows", .when = "vigour > 0 && near.oxygen > 0", .add = {{"growth", "vigour"}}, .flag = "thriving"},
        {.name = "is crowded", .when = "vigour > 0", .set = {{"crowd", "near.growth / 4"}}},
        {.name = "is very crowded", .when = "vigour > 0 && near.growth >= 60", .set = {{"crowd", "1"}}},
        {.name = "crowding stunts", .when = "crowd != 0 && growth <= 40", .take = {{"growth", "crowd"}}},
        {.name = "air takes charge", .when = "phase == gas", .add = {{"charge", "near.wetness * 3 / 8"}}},
        {.name = "grows up", .when = "vigour > 0 && growth >= 30", .become = "grown", .event = "grow_up"},
        {.name = "wilts", .when = "temperature >= 330", .become = "wilted", .event = "wilt"},
    };
    auto program = Program::prepare(definition);
    check.that(program.has_value(), program ? std::string() : std::format("{}: {}", program.error().where, program.error().message));
    return program ? *program : Program{};
}

// Every material somewhere, at every temperature, wet and charged here and there — by a number of the place.
void scatter(World& world, std::uint32_t seed) {
    const std::uint32_t materials = static_cast<std::uint32_t>(world.program.materials().size());
    for (std::uint32_t y = 0; y < world.grid.height; ++y)
        for (std::uint32_t x = 0; x < world.grid.width; ++x) {
            const std::uint32_t chance = mlibs::math::mix32(seed ^ mlibs::math::mix32(static_cast<std::uint32_t>(world.at(x, y))));
            world.program.place(world.state, world.cells(), world.at(x, y), chance % materials);
            if (chance >> 8 & 1) world.heat(x, y, 150 + (chance >> 9) % 1400);
            if ((chance >> 20 & 3) == 0) world.value("wetness", x, y) = chance >> 22 & 63;
            if ((chance >> 28 & 3) == 0) world.value("charge", x, y) = (chance >> 4 & 15) * 1000;
        }
}

void cases(const Subject& subject) {
    using mlibs::cts::Level;
    const auto declare = [&](std::string id, std::vector<std::string> needs, std::function<void(Check&, const Subject&)> body) {
        mlibs::cts::declare({std::move(id), std::string(contract), Level::exact, std::move(needs), [subject, body = std::move(body)](Check& check) { body(check, subject); }});
    };
    declare("carrier.heat-flows", {"grid"}, [](Check& check, const Subject& who) {
        // An iron bar on stone under air, hot at one end: a share of each difference flows a step, in 256ths of a kelvin.
        World world(standard_program(), {12, 3}, "stone");
        world.row(0, "air");
        world.row(1, "iron");
        world.heat(0, 1, 900);
        const std::uint32_t far_before = world.value("temperature", 11, 1);
        const Outcome outcome = compare(check, who, world, 150);
        check.that(outcome.state.size() == world.state.size() && outcome.state[*world.program.channel("temperature") * world.cells() + world.at(11, 1)] > far_before,
                   "the far end of the bar is warmer than it was");
    });
    declare("carrier.fire-spreads", {"grid"}, [](Check& check, const Subject& who) {
        // A strip of wood on stone under air, lit at one end: adjustments, a flag, accumulation, conversions, resets.
        World world(standard_program(), {10, 3}, "stone");
        world.row(0, "air");
        world.row(1, "wood");
        world.heat(0, 1, 700);
        const Outcome outcome = compare(check, who, world, 900);
        check.that(outcome.count(world.program, "ash") == 10, std::format("every piece burnt to ash ({} of 10)", outcome.count(world.program, "ash")));
    });
    declare("carrier.wet-and-dry", {"grid"}, [](Check& check, const Subject& who) {
        // Wood between water and a hot stone: it takes up water (harder to light), and dries in the heat.
        World world(standard_program(), {7, 3}, "air");
        world.row(2, "stone");
        for (std::uint32_t x = 1; x < 6; ++x) world.put(x, 1, "wood");
        world.put(0, 1, "water");
        world.put(6, 1, "stone");
        world.heat(6, 1, 640);
        world.heat(6, 2, 640);
        const Outcome outcome = compare(check, who, world, 400);
        check.that(outcome.changed > 0 && outcome.held > 0, "laws held");
    });
    declare("carrier.phases", {"grid"}, [](Check& check, const Subject& who) {
        // Water around lava and ice: it boils to steam, steam condenses, water freezes, ice melts, lava sets.
        World world(standard_program(), {8, 5}, "water");
        world.row(4, "stone");
        world.put(2, 2, "lava");
        world.put(2, 3, "lava");
        for (std::uint32_t x = 5; x < 8; ++x) world.put(x, 1, "ice");
        world.heat(6, 1, 120);
        world.put(4, 0, "steam");
        const Outcome outcome = compare(check, who, world, 500);
        check.that(outcome.became >= 4, std::format("materials became others ({} times)", outcome.became));
    });
    declare("carrier.charge", {"grid"}, [](Check& check, const Subject& who) {
        // A wire of iron into water, wood after it: charge goes where it is conducted.
        World world(standard_program(), {8, 2}, "air");
        for (std::uint32_t x = 0; x < 4; ++x) world.put(x, 1, "iron");
        world.put(4, 1, "water");
        world.put(5, 1, "water");
        world.put(6, 1, "wood");
        world.put(7, 1, "iron");
        world.value("charge", 0, 1) = 1'000'000;
        const Outcome outcome = compare(check, who, world, 120);
        check.that(outcome.state.size() == world.state.size() && outcome.state[*world.program.channel("charge") * world.cells() + world.at(5, 1)] > 0, "charge reached the water");
    });
    declare("carrier.soup", {"grid"}, [](Check& check, const Subject& who) {
        World world(standard_program(), {24, 16}, "air");
        scatter(world, 0x51ed270bu);
        const Outcome outcome = compare(check, who, world, 300);
        check.that(outcome.became >= 20, std::format("a soup of every material: {} became another", outcome.became));
    });
    declare("carrier.soup-wrapped", {"grid", "wrapped"}, [](Check& check, const Subject& who) {
        // Round the edges — and on a grid 2 wide, where each is beside the other twice.
        for (const Grid grid : {Grid{9, 6, true}, Grid{2, 7, true}, Grid{5, 2, true}}) {
            World world(standard_program(), grid, "air");
            scatter(world, 0x9e3779b9u + grid.width);
            const Outcome outcome = compare(check, who, world, 200);
            check.that(outcome.changed > 0, "it changed");
            if (!outcome.same) break;
        }
    });
    declare("carrier.no-material", {"grid"}, [](Check& check, const Subject& who) {
        // Instances that are no material of the definition, full of everything: nothing acts on them, nothing
        // flows to or from them, and they are nothing to their neighbours — no oxygen, no state to sum.
        World world(standard_program(), {7, 5}, "air");
        scatter(world, 0x2545f491u);
        const std::array<std::pair<std::uint32_t, std::uint32_t>, 5> foreign{{{0, 0}, {3, 2}, {4, 2}, {6, 4}, {1, 3}}};
        for (std::size_t index = 0; index < foreign.size(); ++index) {
            const auto [x, y] = foreign[index];
            world.state[world.at(x, y)] = index % 2 ? none : 9999u;
            for (std::size_t channel = 1; channel < world.program.channels().size(); ++channel) world.state[channel * world.cells() + world.at(x, y)] = 0xfffffff0u + static_cast<std::uint32_t>(channel);
        }
        // Wood that would burn, sealed in by them and by stone: it has no oxygen beside it.
        world.put(2, 2, "wood");
        world.heat(2, 2, 900);
        world.put(2, 1, "stone");
        world.put(2, 3, "stone");
        world.put(1, 2, "stone");
        const Outcome outcome = compare(check, who, world, 200);
        if (outcome.state.size() != world.state.size()) return;
        for (const auto [x, y] : foreign)
            for (std::size_t channel = 0; channel < world.program.channels().size(); ++channel)
                check.equal(outcome.state[channel * world.cells() + world.at(x, y)], world.state[channel * world.cells() + world.at(x, y)],
                            std::format("the reference's own: `{}` of what is no material stays", world.program.channels()[channel].name));
        check.equal(outcome.state[*world.program.channel("burned") * world.cells() + world.at(2, 2)], 0u, "sealed wood burnt nothing");
    });
    declare("carrier.saturation", {"grid"}, [](Check& check, const Subject& who) {
        // Values at the ends of 32 bits: what is added stops at the top, what is taken at 0, a flow is a share of
        // a difference of nearly everything.
        World world(standard_program(), {6, 4}, "air");
        scatter(world, 0x7f4a7c15u);
        for (std::uint32_t y = 0; y < 4; ++y)
            for (std::uint32_t x = 0; x < 6; ++x) {
                const std::uint32_t chance = mlibs::math::mix32(0xa0761d64u + static_cast<std::uint32_t>(world.at(x, y)));
                if (chance & 1) world.value("temperature", x, y) = 0xffffffffu - (chance >> 1 & 0xfff);
                if (chance & 2) world.value("burned", x, y) = 0xffffffffu - (chance >> 13 & 3);
                if (chance & 4) world.value("wetness", x, y) = 0xffffffffu - (chance >> 15 & 0xff);
                if (chance & 8) world.value("charge", x, y) = chance & 16 ? 0xffffffffu : 0u;
            }
        world.put(0, 0, "lava");
        world.value("temperature", 0, 0) = 0xfffffffeu;
        const Outcome outcome = compare(check, who, world, 120);
        check.that(outcome.changed > 0, "it changed");
    });
    declare("carrier.added-material", {"grid"}, [](Check& check, const Subject& who) {
        // Two rows more and no law changed: alcohol beside a fire boils, its vapour catches and burns out.
        Program program = with_alcohol(check);
        if (check.failed()) return;
        World world(std::move(program), {5, 3}, "air");
        world.row(2, "stone");
        world.put(0, 1, "wood");
        world.heat(0, 1, 700);
        for (std::uint32_t x = 1; x < 4; ++x) world.put(x, 1, "alcohol");
        const Outcome outcome = compare(check, who, world, 400);
        check.that(outcome.count(world.program, "alcohol") == 0 && outcome.became >= 6, std::format("the alcohol boiled away ({} conversions)", outcome.became));
    });
    declare("carrier.every-kind", {"grid", "wrapped"}, [](Check& check, const Subject& who) {
        // A definition of its own (the garden above): every kind of read and write a law has.
        const Program program = garden(check);
        if (check.failed()) return;
        for (const Grid grid : {Grid{11, 7, false}, Grid{6, 5, true}}) {
            World world(program, grid, "air");
            for (std::uint32_t y = 0; y < grid.height; ++y)
                for (std::uint32_t x = 0; x < grid.width; ++x) {
                    const std::uint32_t chance = mlibs::math::mix32(0x1b873593u ^ mlibs::math::mix32(static_cast<std::uint32_t>(world.at(x, y)) + grid.width));
                    world.program.place(world.state, world.cells(), world.at(x, y), chance % 5);
                    if ((chance >> 6 & 7) == 0) world.heat(x, y, 300 + (chance >> 9) % 60);
                    if ((chance >> 16 & 3) == 0) world.value("charge", x, y) = chance >> 18 & 0xffff;
                    if ((chance >> 3 & 7) == 0) world.value("growth", x, y) = chance >> 24 & 63;
                }
            const Outcome outcome = compare(check, who, world, 250);
            check.that(outcome.became >= 3, std::format("sprouts grew up, heat wilted ({} conversions)", outcome.became));
            if (!outcome.same) break;
        }
    });
}

constexpr std::size_t count = 11;

}  // namespace

std::vector<Pair> pairs_of(Grid grid) {
    std::vector<Pair> pairs;
    for (std::uint32_t y = 0; y < grid.height; ++y)
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            const std::uint32_t here = y * grid.width + x;
            if (x + 1 < grid.width) pairs.push_back({here, here + 1, 1});
            else if (grid.wrapped && grid.width > 1) pairs.push_back({here, y * grid.width, 1});
            if (y + 1 < grid.height) pairs.push_back({here, here + grid.width, 1});
            else if (grid.wrapped && grid.height > 1) pairs.push_back({here, x, 1});
        }
    return pairs;
}

void declare(Subject subject, mlibs::cts::Against who, std::source_location at) {
    if (who.subject.empty()) who.subject = subject.name;
    if (!who.has) who.has = [has = subject.has](std::string_view what) { return std::ranges::find(has, what) != has.end(); };
    mlibs::cts::against(std::move(who), [subject] { cases(subject); }, at);
}

std::size_t case_count() noexcept { return count; }

}  // namespace xengine::matter::cts
