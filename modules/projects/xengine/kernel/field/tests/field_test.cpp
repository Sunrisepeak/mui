// The air as a carrier of matter (xengine.field; the M6.5 design's T5.2): a cell held hot near the ground — a
// fire's heat — warms the air, the warm air rises (an updraft above it), and with a wind the heat lies downwind of
// it, not upwind; the same bits twice; and a cell's warmth is said to have come by `heat flows`.
import std;
import xengine.field;
import xengine.matter.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace field = xengine::field;
namespace matter = xengine::matter;

namespace {

std::uint32_t stored(const matter::Program& program, std::uint32_t channel, double value) {
    return static_cast<std::uint32_t>(std::lround(std::ldexp(value, static_cast<int>(program.channels()[channel].fraction))));
}

struct Played {
    std::optional<field::Field> air;
    field::Vec3                 above;      // the wind 3 m over the heat
    float                       downwind = 0, upwind = 0, beside = 0;
};

Played play(const matter::Program& program, field::Vec3 wind, int steps) {
    Played made;
    made.air.emplace(std::move(*field::Field::make(program, {.air = *program.material("air"), .wind = wind})));
    field::Field& air = *made.air;
    air.cover({0, 3, 0}, 4);  // eight blocks: every place the case reads
    const std::uint32_t temperature = *program.channel("temperature");
    const field::Vec3 heat{0.5f, 0.5f, 0.5f};
    for (int step = 0; step < steps; ++step) {
        (void)air.set(*air.cell_at(heat), temperature, stored(program, temperature, 900));  // the fire keeps burning
        expect(air.step().has_value());
    }
    made.above = air.wind_at({0.5f, 3.5f, 0.5f});
    made.downwind = air.kelvin_at({3.5f, 1.5f, 0.5f}), made.upwind = air.kelvin_at({-2.5f, 1.5f, 0.5f}), made.beside = air.kelvin_at({0.5f, 1.5f, 3.5f});
    return made;
}

}  // namespace

UTest cases{"field", [] {
    "a fire's heat warms the air, the warm air rises over it, a wind carries the heat downwind; the same bits twice; why a cell is warm"_test = [] {
        const auto table = matter::standard();
        expect(fatal(table.has_value())) << "the standard definition prepares";
        const matter::Program& program = *table;

        const Played still = play(program, {0, 0, 0}, 180);
        std::println("still air, 3 s: the wind 3 m above the fire ({:.2f}, {:.2f}, {:.2f}) m/s; 3 m to one side {:.1f} K, the other {:.1f} K", still.above.x, still.above.y, still.above.z,
                     still.downwind, still.upwind);
        expect(gt(still.above.y, 0.3f)) << "an updraft over the fire";
        expect(lt(std::abs(still.downwind - still.upwind), 0.5f)) << "with no wind, the heat spreads alike either way";

        const Played windy = play(program, {2, 0, 0}, 180);
        std::println("a 2 m/s wind along x, 3 s: 3 m downwind {:.1f} K, 3 m upwind {:.1f} K, 3 m across {:.1f} K", windy.downwind, windy.upwind, windy.beside);
        expect(gt(windy.downwind - windy.upwind, 2.0f)) << "the wind carries the heat downwind";

        const Played again = play(program, {2, 0, 0}, 180);
        expect(eq(again.air->state_hash(), windy.air->state_hash())) << "the same bits twice";

        // Why the cell over the fire is warm: heat flowed in from the one below, by `heat flows`.
        const std::uint32_t over = *windy.air->cell_at({0.5f, 1.5f, 0.5f}), fire = *windy.air->cell_at({0.5f, 0.5f, 0.5f});
        const auto why = windy.air->why(over);
        expect(fatal(why.has_value()));
        const bool from_fire = std::ranges::any_of(why->flows, [&](const matter::Flowed& flow) { return flow.in && flow.beside == fire && program.laws()[flow.law].name == "heat flows"; });
        expect(from_fire) << "heat in from the cell below, by `heat flows`";
    };
}};
