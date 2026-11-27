// Matter carried by 3D bodies (meng.dynamics.objects; the M6.5 design's T5.1): crates of wood on a floor in the
// air. One is set alight; the crate leaning on it catches from it — heat across what touches, by the standard
// definition's laws, nothing here naming a law — and the crate standing apart does not. The bodies go to sleep
// and still touch; the same world twice is the same bits; and the carrier says why the second crate caught.
import std;
import meng.dynamics.objects;
import meng.field;
import meng.matter.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace rigid3 = meng::dynamics::rigid3;
namespace matter = meng::matter;

namespace {

struct Yard {
    std::optional<rigid3::World>   world;
    std::optional<rigid3::Objects> objects;
    rigid3::BodyId                 floor, lit, leaning, apart;
};

std::uint32_t stored(const matter::Program& program, std::uint32_t channel, double value) {
    return static_cast<std::uint32_t>(std::lround(std::ldexp(value, static_cast<int>(program.channels()[channel].fraction))));
}

Yard yard(const matter::Program& program) {
    Yard made;
    made.world.emplace(std::move(*rigid3::World::make({})));
    rigid3::World& world = *made.world;
    made.floor = *world.add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
    const auto crate = [&](float x) { return *world.add({.at = {x, 0.5f, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), {.density = 600}); };
    made.lit = crate(0), made.leaning = crate(1.01f), made.apart = crate(4);
    made.objects.emplace(std::move(*rigid3::Objects::make(program, {.ambient = *program.material("air")})));
    rigid3::Objects& objects = *made.objects;
    // The floor stands, its matter not carried: what touches everything is the field's and the terrain's (T5.2).
    for (const rigid3::BodyId body : {made.lit, made.leaning, made.apart}) (void)objects.add(body, *program.material("wood"));
    return made;
}

}  // namespace

UTest cases{"dynamics.objects", [] {
    "a crate set alight lights the crate leaning on it, not the one apart; asleep they still touch; the same bits twice; why it caught"_test = [] {
        const auto table = matter::standard();
        expect(fatal(table.has_value())) << "the standard definition prepares";
        const matter::Program& program = *table;
        const std::uint32_t temperature = *program.channel("temperature"), burning = *program.channel("burning");
        const auto run = [&](int steps, std::uint64_t* caught_at, std::optional<matter::Why>* why_caught) {
            Yard made = yard(program);
            (void)made.objects->set(made.lit, temperature, stored(program, temperature, 900));
            for (int step = 1; step <= steps; ++step) {
                made.world->step();
                // The fire kept going: the first crate is held at its flame (a torch's), as a person would hold one.
                if (step < 120) (void)made.objects->set(made.lit, temperature, std::max(*made.objects->get(made.lit, temperature), stored(program, temperature, 900)));
                const auto stepped = made.objects->step(*made.world);
                expect(fatal(stepped.has_value())) << (stepped ? "" : stepped.error().message);
                if (caught_at && !*caught_at && *made.objects->get(made.leaning, burning) > 0) {
                    *caught_at = static_cast<std::uint64_t>(step);
                    if (why_caught) *why_caught = made.objects->why(made.leaning);
                }
            }
            return made;
        };
        std::uint64_t caught = 0;
        std::optional<matter::Why> why;
        Yard played = run(1200, &caught, &why);
        const bool apart_burned = *played.objects->get(played.apart, burning) > 0 || *played.objects->material(played.apart) != *program.material("wood");
        // Asleep, the touching pairs are still there.
        const bool asleep = !played.world->pose(played.leaning)->awake;
        std::vector<rigid3::Touch> touching;
        played.world->touching(touching);
        const bool still = std::ranges::any_of(touching, [&](const rigid3::Touch& touch) { return touch.one == played.lit && touch.other == played.leaning; });
        const auto pair = std::ranges::find_if(touching, [&](const rigid3::Touch& touch) { return touch.one == played.lit && touch.other == played.leaning; });
        std::println("the leaning crate caught at step {} ({:.1f} s); the one apart {}; asleep {}, still touching {} (shared {:.2f} m²); hash {:016x}", caught, static_cast<double>(caught) / 60,
                     apart_burned ? "burned too" : "untouched", asleep, still, pair != touching.end() ? pair->area : 0.0f, played.objects->state_hash());
        expect(gt(caught, std::uint64_t{0})) << "heat across the crates that touch";
        expect(!apart_burned) << "nothing reaches the crate that touches neither";
        expect(asleep && still) << "a stack asleep still touches";
        expect(pair != touching.end() && gt(pair->area, 0.5f) && lt(pair->area, 1.5f)) << "two crates' faces, a square metre";

        // Why it caught: the law that burns it held, and heat flowed in from the crate it leans on.
        expect(fatal(why.has_value()));
        const auto burns = std::ranges::find_if(program.laws(), [](const matter::Law& law) { return law.name == "burns"; });
        const auto law_of = [&](std::uint32_t law) -> std::string_view { return law < program.laws().size() ? std::string_view(program.laws()[law].name) : std::string_view("no law"); };
        const bool burned_by_law = std::ranges::any_of(why->laws, [&](const matter::Judged& judged) { return judged.holds && law_of(judged.law) == "burns"; });
        const auto lit_instance = static_cast<std::uint32_t>(std::ranges::find(played.objects->bodies(), played.lit) - played.objects->bodies().begin());
        const bool heated_by_lit = std::ranges::any_of(why->flows, [&](const matter::Flowed& flow) { return flow.in && flow.beside == lit_instance && law_of(flow.law) == "heat flows"; });
        expect(burns != program.laws().end() && burned_by_law) << "`burns` held";
        expect(heated_by_lit) << "the heat came from the crate it leans on, by `heat flows`";

        // Explained back to where it began: the leaning crate caught by `burns`, the heat in from the crate it leans
        // on by `heat flows`; that one was heated from outside.
        const auto chain = played.objects->explain(played.leaning);
        std::string said;
        for (const matter::Explained& link : chain)
            said += std::format(" body {} at step {}: {}{};", rigid3::body_of(link.who).value, link.cause.step,
                                link.cause.kind == matter::CauseKind::applied ? std::string("applied from outside") : std::string(law_of(link.cause.law)),
                                link.cause.from ? std::format(", the most in by `{}` from body {}", law_of(link.cause.inflow), rigid3::body_of(link.cause.from).value) : std::string());
        std::println("explained:{}", said);
        expect(fatal(ge(chain.size(), std::size_t{2})));
        const auto caught_link = std::ranges::find_if(chain, [&](const matter::Explained& link) { return link.who == rigid3::who(played.leaning) && link.cause.kind == matter::CauseKind::rose; });
        expect(caught_link != chain.end() && law_of(caught_link->cause.law) == "burns" && caught_link->cause.from == rigid3::who(played.lit) && law_of(caught_link->cause.inflow) == "heat flows")
            << said;
        expect(chain.back().who == rigid3::who(played.lit) && chain.back().cause.kind == matter::CauseKind::applied) << said;

        // The same world played again: the same bits.
        Yard again = run(1200, nullptr, nullptr);
        expect(eq(again.objects->state_hash(), played.objects->state_hash()));
    };

    "what is bigger has more to burn: a crate, a block and a plank of the same wood, lit alike, burn out by how much wood each has and how much of it shows"_test = [] {
        const auto table = matter::standard();
        expect(fatal(table.has_value())) << "the standard definition prepares";
        const matter::Program& program = *table;
        const std::uint32_t temperature = *program.channel("temperature");
        auto world = rigid3::World::make({});
        expect(fatal(world.has_value()));
        (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
        // A crate of 1 m (600 kg: 12 units of 50 kg, a cube of them showing 12^⅔ ≈ 5), a block of 0.3 m (16 kg: one
        // unit), a plank 4 m × 0.1 m × 1 m (240 kg: 5 units, all of them at its faces).
        const rigid3::BodyId crate = *world->add({.at = {0, 0.5f, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), {.density = 600});
        const rigid3::BodyId block = *world->add({.at = {5, 0.15f, 0}}, rigid3::Shape::box({0.15f, 0.15f, 0.15f}), {.density = 600});
        const rigid3::BodyId plank = *world->add({.at = {10, 0.05f, 0}}, rigid3::Shape::box({2, 0.05f, 0.5f}), {.density = 600});
        auto objects = rigid3::Objects::make(program, {.ambient = *program.material("air")});
        expect(fatal(objects.has_value()));
        const std::array bodies{crate, block, plank};
        for (const rigid3::BodyId body : bodies) (void)objects->add(body, *program.material("wood"));
        std::array<int, 3> out{0, 0, 0};
        for (int step = 1; step <= 2400 && std::ranges::count(out, 0) > 0; ++step) {
            world->step();
            for (const rigid3::BodyId body : bodies)  // held at a torch's heat for the first second, alike
                if (step < 60 && *objects->material(body) == *program.material("wood"))
                    (void)objects->set(body, temperature, std::max(*objects->get(body, temperature), stored(program, temperature, 900)));
            expect(fatal(objects->step(*world).has_value()));
            for (std::size_t at = 0; at < bodies.size(); ++at)
                if (!out[at] && *objects->material(bodies[at]) != *program.material("wood")) out[at] = step;
        }
        std::println("burned out: the crate at step {} ({:.1f} s), the block at {} ({:.1f} s), the plank at {} ({:.1f} s)", out[0], out[0] / 60.0, out[1], out[1] / 60.0, out[2], out[2] / 60.0);
        expect(fatal(gt(out[0], 0) && gt(out[1], 0) && gt(out[2], 0))) << "each burned out";
        // The block is one unit: its 400 of fuel at 1 a step. The crate has 12 units' fuel burned over 5 at a time:
        // 960 steps. The plank shows all it has: as fast as the block.
        expect(ge(out[0], 900) && le(out[0], 1020)) << "the crate: 400 × 12 ÷ 5 steps of burning";
        expect(ge(out[1], 380) && le(out[1], 460)) << "the block: 400 steps";
        expect(lt(std::abs(out[2] - out[1]), 30)) << "the plank burns as fast as the block: all of it shows";
    };

    "in the air of a field: a burning crate warms the air over it, and its updraft lifts a sheet of cloth held over it (the emergence set's 9, in 3D)"_test = [] {
        const auto table = matter::standard();
        expect(fatal(table.has_value())) << "the standard definition prepares";
        const matter::Program& program = *table;
        const std::uint32_t temperature = *program.channel("temperature");
        auto world = rigid3::World::make({});
        const auto floor = *world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
        (void)floor;
        const auto crate = *world->add({.kind = rigid3::Kind::fixed, .at = {0.5f, 0.5f, 0.5f}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}));
        const rigid3::Vec start{0.5f, 2.6f, 0.5f};
        // As light as a sheet of paper: 20 cm square, 8 g.
        const auto sheet = *world->add({.at = start}, rigid3::Shape::box({0.1f, 0.005f, 0.1f}), {.density = 20});
        auto objects = rigid3::Objects::make(program);
        auto air = meng::field::Field::make(program, {.air = *program.material("air")});
        expect(fatal(objects.has_value() && air.has_value()));
        (void)objects->add(crate, *program.material("wood")), (void)objects->add(sheet, *program.material("cloth"));
        air->cover({0.5f, 3, 0.5f}, 4);
        float highest = start.y, warmest = 0;
        for (int step = 0; step < 240; ++step) {
            (void)objects->set(crate, temperature, std::max(*objects->get(crate, temperature), stored(program, temperature, 900)));  // alight
            objects->blow(*world, *air);
            world->step();
            const auto stepped = objects->step(*world, *air);
            expect(fatal(stepped.has_value())) << (stepped ? "" : stepped.error().message);
            highest = std::max(highest, world->pose(sheet)->centre.y);
            warmest = std::max(warmest, air->kelvin_at({0.5f, 1.5f, 0.5f}));
        }
        const auto lift = air->wind_at({0.5f, 2, 0.5f});
        std::println("over the burning crate: the air 1 m above as warm as {:.0f} K, the wind 1.5 m above ({:.2f}, {:.2f}, {:.2f}) m/s; the sheet of cloth from {:.2f} m up to {:.2f} m", warmest, lift.x, lift.y,
                     lift.z, start.y, highest);
        expect(gt(warmest, 320.0f)) << "the crate warms the air over it";
        expect(gt(lift.y, 1.0f)) << "warm air rises";
        // Lifted against its weight while it is in the plume's core (a quarter metre and more is no wobble); out of
        // the core it falls again.
        expect(gt(highest, start.y + 0.25f)) << "the updraft lifts a light thing";
    };

    "in the rain, lightning strikes the iron pole and not the wooden one as tall; wet wood heated as dry wood is does not catch (the emergence set's 11, and 2's wet half)"_test = [] {
        const auto table = matter::standard();
        expect(fatal(table.has_value())) << "the standard definition prepares";
        const matter::Program& program = *table;
        const std::uint32_t temperature = *program.channel("temperature"), burning = *program.channel("burning"), charge = *program.channel("charge");
        auto world = rigid3::World::make({});
        (void)world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
        // Two poles stood in the ground, as tall as each other; two crates, one under a roof.
        const auto pole = [&](float x) { return *world->add({.kind = rigid3::Kind::fixed, .at = {x, 1, 0}}, rigid3::Shape::box({0.05f, 1, 0.05f})); };
        const auto iron = pole(-2), wooden = pole(2);
        const auto out = *world->add({.kind = rigid3::Kind::fixed, .at = {-2, 0.5f, 5}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}));
        const auto sheltered = *world->add({.kind = rigid3::Kind::fixed, .at = {2, 0.5f, 5}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}));
        (void)world->add({.kind = rigid3::Kind::fixed, .at = {2, 3, 5}}, rigid3::Shape::box({1.5f, 0.1f, 1.5f}));  // the roof
        auto objects = rigid3::Objects::make(program, {.ambient = *program.material("air")});
        expect(fatal(objects.has_value()));
        (void)objects->add(iron, *program.material("iron")), (void)objects->add(wooden, *program.material("wood"));
        (void)objects->add(out, *program.material("wood")), (void)objects->add(sheltered, *program.material("wood"));
        // Ten seconds of rain: what is open to the sky wets.
        for (int step = 0; step < 600; ++step) {
            objects->rain(*world, 1);
            world->step();
            expect(objects->step(*world).has_value());
        }
        const std::uint32_t wet = *objects->get(out, *program.channel("wetness")), dry = *objects->get(sheltered, *program.channel("wetness"));
        const auto struck = objects->lightning(*world);
        std::println("after ten seconds of rain: the crate in it wetness {}, the one under the roof {}; lightning struck {} (iron pole {}, wooden {}); the iron pole's charge {}, {:.0f} K",
                     wet, dry, struck ? struck->value : 0u, iron.value, wooden.value, *objects->get(iron, charge),
                     std::ldexp(static_cast<double>(*objects->get(iron, temperature)), -static_cast<int>(program.channels()[temperature].fraction)));
        expect(gt(wet, dry)) << "the rain wets what is open to the sky";
        expect(struck.has_value() && *struck == iron) << "it strikes what conducts, and not wood as tall";
        expect(gt(*objects->get(iron, charge), 0u) && eq(*objects->get(wooden, charge), 0u));
        // The same heat on both crates for a second: the dry one catches, the wet one does not.
        for (int step = 0; step < 60; ++step) {
            for (const rigid3::BodyId crate : {out, sheltered}) (void)objects->set(crate, temperature, stored(program, temperature, 650));
            world->step();
            expect(objects->step(*world).has_value());
        }
        std::println("held at 650 K a second: the wet crate burning {}, the dry one {}", *objects->get(out, burning), *objects->get(sheltered, burning));
        expect(eq(*objects->get(out, burning), 0u) && gt(*objects->get(sheltered, burning), 0u)) << "wet wood is harder to light";
    };

    "a strip of grass lit in the middle burns faster downwind than upwind (the emergence set's 10)"_test = [] {
        const auto table = matter::standard();
        expect(fatal(table.has_value())) << "the standard definition prepares";
        const matter::Program& program = *table;
        const std::uint32_t temperature = *program.channel("temperature"), burning = *program.channel("burning");
        auto world = rigid3::World::make({});
        auto objects = rigid3::Objects::make(program);
        auto air = meng::field::Field::make(program, {.air = *program.material("air"), .wind = {2, 0, 0}});
        expect(fatal(objects.has_value() && air.has_value()));
        // Tufts 0.25 m long end to end along x (fixed: grass does not fall over), from −3.5 m to 3.5 m.
        std::vector<rigid3::BodyId> tufts;
        for (int tuft = -14; tuft <= 14; ++tuft) {
            tufts.push_back(*world->add({.kind = rigid3::Kind::fixed, .at = {static_cast<float>(tuft) * 0.25f, 0.15f, 0.5f}}, rigid3::Shape::box({0.125f, 0.15f, 0.05f})));
            (void)objects->add(tufts.back(), *program.material("wood"));
        }
        air->cover({0, 2, 0.5f}, 6);
        const rigid3::BodyId middle = tufts[14], downwind = tufts[14 + 12], upwind = tufts[14 - 12];
        std::uint64_t down_at = 0, up_at = 0;
        for (int step = 1; step <= 1800 && (!down_at || !up_at); ++step) {
            if (step < 30) (void)objects->set(middle, temperature, stored(program, temperature, 900));
            world->step();
            const auto stepped = objects->step(*world, *air);
            expect(fatal(stepped.has_value())) << (stepped ? "" : stepped.error().message);
            if (!down_at && *objects->get(downwind, burning) > 0) down_at = static_cast<std::uint64_t>(step);
            if (!up_at && *objects->get(upwind, burning) > 0) up_at = static_cast<std::uint64_t>(step);
        }
        std::println("a 2 m/s wind: the fire 3 m downwind at step {}, 3 m upwind at step {} (0: not in 30 s)", down_at, up_at);
        // Explained: from the downwind tuft back, tuft by tuft and through the air, to the one that was lit.
        const auto chain = objects->explain(downwind, 64);
        std::size_t through_air = 0;
        for (const matter::Explained& link : chain) through_air += link.cause.through.has_value();
        std::println("explained in {} links ({} through the air), ending at body {} ({}); the lit one is {}", chain.size(), through_air, chain.empty() ? 0u : rigid3::body_of(chain.back().who).value,
                     chain.empty() ? "" : chain.back().cause.kind == matter::CauseKind::applied ? "applied" : "not applied", middle.value);
        expect(!chain.empty() && chain.back().who == rigid3::who(middle) && chain.back().cause.kind == matter::CauseKind::applied) << "back to the tuft that was lit";
        expect(gt(down_at, std::uint64_t{0})) << "it spreads downwind";
        expect(up_at == 0 || lt(down_at, up_at)) << "faster downwind than upwind";
    };
}};
