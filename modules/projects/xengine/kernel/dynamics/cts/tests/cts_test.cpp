// The dynamics conformance on the CPU reference: every case passes; a
// subject without a family has that family's cases unsupported (never
// passed), and the run fails when it requires the family.
import std;
import xengine.dynamics.cts;
import mcpplibs.mtest;

using mcpplibs::mtest::UTest, mcpplibs::mtest::expect, mcpplibs::mtest::operator""_test;
namespace suite = xengine::dynamics::cts;
using mlibs::cts::Status;

UTest cases{"dynamics.cts", [] {
    // The CPU reference: every case, as this program's own (cpu-reference/collision.sweep.face, …).
    suite::declare();
    // One that has collision alone: the other families' cases are skipped, `unsupported: xpbd`.
    suite::declare({.name = "collision-only", .families = {"collision"}});

    "the CPU reference passes every case"_test = [] {
        const auto tally = mlibs::cts::tally([] { suite::declare(); });
        expect(tally.ok() && tally.count(Status::passed) == suite::case_count() && suite::case_count() >= 6) << tally.text();
    };
    "a subject without a family: its cases unsupported, failed when required"_test = [] {
        const auto partial = mlibs::cts::tally([] { suite::declare({.name = "collision-only", .families = {"collision"}}); });
        expect(partial.ok() && partial.count(Status::unsupported) > 0) << "unsupported is not passed, nor failed";
        const auto required = mlibs::cts::tally([] { suite::declare({.name = "collision-only", .families = {"collision"}}, {.required = {"xpbd"}}); });
        expect(!required.ok() && required.ids(Status::failed) == std::vector<std::string>{"requires xpbd"}) << "a required family missing fails";
    };
}};
