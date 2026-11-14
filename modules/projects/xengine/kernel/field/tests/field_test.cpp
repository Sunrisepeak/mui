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

    "streaming: a block parked and expanded is the same cells; a walker crossing the harbour, the field following it, keeps every total exactly at each switch with nothing to see change about the walker; regions are values (taken, written, read, put back) and a field copied is a branch; a parked block still warms the air beside it"_test = [] {
        const auto table = matter::standard();
        expect(fatal(table.has_value())) << "the standard definition prepares";
        const matter::Program& program = *table;
        const std::uint32_t temperature = *program.channel("temperature"), wetness = *program.channel("wetness");
        auto air = field::Field::make(program, {.air = *program.material("air")});
        expect(fatal(air.has_value()));
        air->cover({4, 4, 4}, 2);  // one block
        // Uneven: a warm corner, a wet one.
        (void)air->set(*air->cell_at({0.5f, 0.5f, 0.5f}), temperature, stored(program, temperature, 700));
        (void)air->set(*air->cell_at({6.5f, 2.5f, 3.5f}), wetness, 40);
        for (int step = 0; step < 30; ++step) expect(air->step().has_value());
        std::vector<float> before;
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) before.push_back(air->kelvin_at({static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<float>(z) + 0.5f}));
        const std::int64_t heat = air->total(temperature), water = air->total(wetness);
        expect(air->park({4, 4, 4}) && eq(air->cells(), std::size_t{0}) && eq(air->parked(), std::size_t{1}));
        expect(eq(air->total(temperature), heat) && eq(air->total(wetness), water)) << "parked: the totals as they were";
        expect(air->expand({4, 4, 4}) && eq(air->parked(), std::size_t{0}));
        std::vector<float> after;
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) after.push_back(air->kelvin_at({static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<float>(z) + 0.5f}));
        expect(before == after) << "expanded at once: every cell as it was";

        // A walker from x = 0 to 60 m along the quay, past a fire at the start; the field follows it (blocks within
        // 6 m expanded, beyond 12 m parked). At every switch the totals are what they were just before it — plus the
        // fresh air of any block made for the first time (512 cells at 293 K, no water).
        auto quay = field::Field::make(program, {.air = *program.material("air")});
        expect(fatal(quay.has_value()));
        quay->follow({0, 2, 0}, 6, 12);
        // The same walk with nothing ever parked: what the streamed field is measured against near the walker.
        auto whole = *quay;
        // What is seen about the walker: the air's warmth and wind at 27 points within 4 m.
        const auto seen = [](const field::Field& air, field::Vec3 at) {
            std::vector<float> out;
            for (int z = -1; z <= 1; ++z)
                for (int y = -1; y <= 1; ++y)
                    for (int x = -1; x <= 1; ++x) {
                        const field::Vec3 point{at.x + 4.0f * static_cast<float>(x), at.y + 2.0f * static_cast<float>(y), at.z + 4.0f * static_cast<float>(z)};
                        const field::Vec3 wind = air.wind_at(point);
                        out.insert(out.end(), {air.kelvin_at(point), wind.x, wind.y, wind.z});
                    }
            return out;
        };
        float jump = 0, drift = 0;
        const auto blocks = [&] { return quay->cells() / field::block_cells + quay->parked(); };
        std::size_t switches = 0, exact = 0, most_parked = 0;
        for (int step = 0; step < 300; ++step) {
            const field::Vec3 walker{static_cast<float>(step) * 0.2f, 2, 0};
            if (step < 100) {
                (void)quay->set(*quay->cell_at({0.5f, 0.5f, 0.5f}), temperature, stored(program, temperature, 900));
                (void)whole.set(*whole.cell_at({0.5f, 0.5f, 0.5f}), temperature, stored(program, temperature, 900));
            }
            expect(quay->step().has_value());
            expect(whole.step().has_value());
            whole.follow(walker, 6, 1e9f);
            const std::vector<float> before_switch = seen(*quay, walker);
            const std::int64_t heat_before = quay->total(temperature), water_before = quay->total(wetness);
            const std::size_t cells_before = quay->cells(), parked_before = quay->parked(), blocks_before = blocks();
            quay->follow(walker, 6, 12);
            const std::vector<float> after_switch = seen(*quay, walker), unstreamed = seen(whole, walker);
            for (std::size_t value = 0; value < after_switch.size(); ++value)
                jump = std::max(jump, std::abs(after_switch[value] - before_switch[value])), drift = std::max(drift, std::abs(after_switch[value] - unstreamed[value]));
            if (quay->cells() != cells_before || quay->parked() != parked_before) {
                ++switches;
                const auto made = static_cast<std::int64_t>(blocks() - blocks_before);
                const std::int64_t fresh = made * static_cast<std::int64_t>(field::block_cells) * static_cast<std::int64_t>(stored(program, temperature, 293));
                exact += quay->total(temperature) == heat_before + fresh && quay->total(wetness) == water_before;
            }
            most_parked = std::max(most_parked, quay->parked());
        }
        // A parked block (the fire's, far behind the walker now) still warms the air beside it: the totals keep
        // the heat it had, and it was not frozen.
        std::println("a walker 60 m along: {} switches, {} with every total exact; {} blocks parked at most, {} cells expanded at the end", switches, exact, most_parked, quay->cells());
        expect(gt(switches, std::size_t{3})) << "the field followed the walker";
        expect(eq(exact, switches)) << "every switch keeps the totals exactly";
        expect(gt(most_parked, std::size_t{0}));
        std::println("about the walker: the most a switch changed what is seen {} (K or m/s); the most it differs from the field never parked {:.4f}", jump, drift);
        expect(eq(jump, 0.0f)) << "a switch changes nothing within 4 m of the walker: no jump to see";
        expect(lt(drift, 0.05f)) << "streamed or not, the air about the walker is the same to a twentieth";

        // Regions are values: one taken out, written, read and put back is the field as it was; a copy is a branch.
        {
            auto air = field::Field::make(program, {.air = *program.material("air"), .wind = {1, 0, 0}});
            expect(fatal(air.has_value()));
            air->cover({4, 4, 4}, 2), air->cover({12, 4, 4}, 2);
            (void)air->set(*air->cell_at({10.5f, 3.5f, 2.5f}), temperature, stored(program, temperature, 650));
            (void)air->set(*air->cell_at({9.5f, 1.5f, 6.5f}), wetness, 30);
            for (int step = 0; step < 20; ++step) expect(air->step().has_value());
            expect(air->park({12, 4, 4}));
            const std::uint64_t parked_hash = air->state_hash();
            const std::int64_t heat = air->total(temperature);
            auto region = air->take({12, 4, 4});
            expect(fatal(region.has_value()));
            expect(eq(air->parked(), std::size_t{0}) && ne(air->total(temperature), heat)) << "taken out: its heat went with it";
            const std::vector<std::uint8_t> written = region->bytes();
            const auto read = field::Region::read(written);
            expect(fatal(read.has_value())) << (read ? "" : read.error());
            expect(*read == *region) << "read as written";
            expect(!field::Region::read(std::span(written).first(written.size() - 1)).has_value()) << "a region cut short is refused";
            expect(air->put(*read) && eq(air->state_hash(), parked_hash) && eq(air->total(temperature), heat)) << "put back: the field as it was, bit for bit";
            expect(!air->put(*read)) << "not twice";
            std::println("a region: {} bytes; put back, the field's hash {:016x} as before", written.size(), air->state_hash());

            field::Field branch = *air;  // a value: copied, it goes its own way
            for (int step = 0; step < 10; ++step) expect(air->step().has_value() && branch.step().has_value());
            expect(eq(branch.state_hash(), air->state_hash())) << "the same steps, the same bits";
            (void)branch.set(*branch.cell_at({4.5f, 0.5f, 4.5f}), temperature, stored(program, temperature, 900));
            for (int step = 0; step < 10; ++step) expect(air->step().has_value() && branch.step().has_value());
            expect(ne(branch.state_hash(), air->state_hash()) && gt(branch.kelvin_at({4.5f, 1.5f, 4.5f}), air->kelvin_at({4.5f, 1.5f, 4.5f}) + 1.0f)) << "a fire on the branch alone";
        }

        // A parked hot block warms the expanded cells beside it.
        auto pair = field::Field::make(program, {.air = *program.material("air")});
        expect(fatal(pair.has_value()));
        pair->cover({4, 4, 4}, 2), pair->cover({12, 4, 4}, 2);  // two blocks side by side along x
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) (void)pair->set(*pair->cell_at({static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<float>(z) + 0.5f}), temperature, stored(program, temperature, 600));
        expect(pair->park({4, 4, 4}));
        const float cool = pair->kelvin_at({8.5f, 4.5f, 4.5f});
        for (int step = 0; step < 120; ++step) expect(pair->step().has_value());
        const float warmed = pair->kelvin_at({8.5f, 4.5f, 4.5f});
        std::println("beside a parked block at 600 K: the cell next to it from {:.1f} K to {:.1f} K in 2 s", cool, warmed);
        expect(gt(warmed, cool + 5.0f)) << "parked, not frozen: it still gives its heat to the air beside it";
    };
}};
