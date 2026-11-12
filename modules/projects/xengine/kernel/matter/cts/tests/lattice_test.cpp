// The lattice as a carrier of matter (xengine.cellular's feature `matter`): a matter program lowered to a
// lattice program, through the carrier suite — the reference step's bits, every tile run and sparsely — with
// lattices wrong in one thing, what the lowered program is, and a step's events. What the lattice adds to the
// reference, cells that move, is emergence_test.cpp's.
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
        expect(eq(plan->transfer_count(), 4u)) << "heat, charge, wetness, pressure";
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
