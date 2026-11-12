// The carrier suite on the reference itself — every case's world does what the case says of it — and on
// carriers that are wrong in one thing each: the suite says where.
import std;
import xengine.matter.cts;
import mcpplibs.mtest;

using mcpplibs::mtest::UTest, mcpplibs::mtest::expect, mcpplibs::mtest::eq, mcpplibs::mtest::operator""_test;
namespace suite = xengine::matter::cts;
namespace matter = xengine::matter;
using mlibs::cts::Status;

namespace {

// The reference on other pairs than the grid's: a carrier that is wrong about who is beside whom.
suite::Subject on_pairs(std::string name, std::function<void(std::vector<matter::Pair>&)> change) {
    return {.name = std::move(name), .carry = [change = std::move(change)](const matter::Program& program, suite::Grid grid) -> suite::Step {
                auto pairs = suite::pairs_of(grid);
                change(pairs);
                return [program, pairs = std::move(pairs), workspace = std::make_shared<matter::Workspace>(), cells = static_cast<std::size_t>(grid.width) * grid.height](
                           std::span<const std::uint32_t> state, std::span<std::uint32_t> next) { return program.step({cells, pairs}, state, next, *workspace).has_value(); };
            }};
}

}  // namespace

UTest cases{"matter.cts", [] {
    // The reference: every case, as this program's own (reference/carrier.heat-flows, …).
    suite::declare();
    // The reference again through the carrier's door: its own pairs, the same bits.
    suite::declare(on_pairs("reference-carried", [](std::vector<matter::Pair>&) {}));

    "every case's world does what the case says of it, and the reference carried is the reference"_test = [] {
        const auto tally = mlibs::cts::tally([] { suite::declare(); });
        expect(tally.ok() && eq(tally.count(Status::passed), suite::case_count())) << tally.text();
        const auto carried = mlibs::cts::tally([] { suite::declare(on_pairs("reference-carried", [](std::vector<matter::Pair>&) {})); });
        expect(carried.ok() && eq(carried.count(Status::passed), suite::case_count())) << carried.text();
    };
    "a carrier wrong in one thing fails: a pair missed, a pair counted twice, the pairs in another order is not wrong"_test = [] {
        const auto missed = mlibs::cts::tally([] { suite::declare(on_pairs("misses-a-pair", [](std::vector<matter::Pair>& pairs) { pairs.pop_back(); })); });
        expect(!missed.ok()) << missed.text();
        expect(missed.count(Status::failed) >= 6u) << "one pair of a grid's: most worlds part\n" << missed.text();
        const auto twice = mlibs::cts::tally([] { suite::declare(on_pairs("counts-a-pair-twice", [](std::vector<matter::Pair>& pairs) { pairs.push_back(pairs.front()); })); });
        expect(!twice.ok()) << twice.text();
        const auto reversed = mlibs::cts::tally([] {
            suite::declare(on_pairs("pairs-reversed", [](std::vector<matter::Pair>& pairs) {
                std::ranges::reverse(pairs);
                for (matter::Pair& pair : pairs) std::swap(pair.first, pair.second);
            }));
        });
        expect(reversed.ok() && eq(reversed.count(Status::passed), suite::case_count())) << reversed.text();
    };
    "a carrier without wrapped grids: those cases unsupported, never passed; one that cannot be readied fails"_test = [] {
        suite::Subject flat = on_pairs("flat", [](std::vector<matter::Pair>&) {});
        flat.has = {"grid"};
        const auto partial = mlibs::cts::tally([&] { suite::declare(flat); });
        expect(partial.ok() && eq(partial.count(Status::unsupported), std::size_t{2}) && eq(partial.count(Status::passed), suite::case_count() - 2)) << partial.text();
        const auto required = mlibs::cts::tally([&] { suite::declare(flat, {.required = {"wrapped"}}); });
        expect(!required.ok()) << "a required capability missing fails";
        const auto unready = mlibs::cts::tally([] { suite::declare({.name = "unready", .carry = [](const matter::Program&, suite::Grid) { return suite::Step{}; }}); });
        expect(eq(unready.count(Status::failed), suite::case_count())) << unready.text();
    };
}};
