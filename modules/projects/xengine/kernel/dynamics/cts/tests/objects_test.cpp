// Matter carried by 3D bodies (xengine.dynamics.objects; the M6.5 design's T5.1): crates of wood on a floor in the
// air. One is set alight; the crate leaning on it catches from it — heat across what touches, by the standard
// definition's laws, nothing here naming a law — and the crate standing apart does not. The bodies go to sleep
// and still touch; the same world twice is the same bits; and the carrier says why the second crate caught.
import std;
import xengine.dynamics.objects;
import xengine.matter.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace rigid3 = xengine::dynamics::rigid3;
namespace matter = xengine::matter;

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
        const auto law_of = [&](std::uint32_t law) -> std::string_view { return program.laws()[law].name; };
        const bool burned_by_law = std::ranges::any_of(why->laws, [&](const matter::Judged& judged) { return judged.holds && law_of(judged.law) == "burns"; });
        const auto lit_instance = static_cast<std::uint32_t>(std::ranges::find(played.objects->bodies(), played.lit) - played.objects->bodies().begin());
        const bool heated_by_lit = std::ranges::any_of(why->flows, [&](const matter::Flowed& flow) { return flow.in && flow.beside == lit_instance && law_of(flow.law) == "heat flows"; });
        expect(burns != program.laws().end() && burned_by_law) << "`burns` held";
        expect(heated_by_lit) << "the heat came from the crate it leans on, by `heat flows`";

        // The same world played again: the same bits.
        Yard again = run(1200, nullptr, nullptr);
        expect(eq(again.objects->state_hash(), played.objects->state_hash()));
    };
}};
