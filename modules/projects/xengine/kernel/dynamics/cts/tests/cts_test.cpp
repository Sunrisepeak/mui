// The dynamics conformance on the CPU reference: every case passes; a
// subject without a family has that family's cases unsupported (never
// passed), and failed when the runner requires it; the report as JSON.
import std;
import xengine.dynamics.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace cts = xengine::dynamics::cts;

UTest cases{"dynamics.cts", [] {
    "the CPU reference passes every case"_test = [] {
        const auto report = cts::run();
        std::println("{}", report.text());
        expect(report.ok() && report.count(mlibs::cts::Status::passed) == report.outcomes.size() && report.outcomes.size() >= 6) << report.text();
        expect(report.json().contains("\"suite\"")) << "a machine-readable report";
    };
    "a subject without a family: its cases unsupported, failed when required"_test = [] {
        const auto partial = cts::run({.name = "collision-only", .families = {"collision"}});
        expect(partial.ok() && partial.count(mlibs::cts::Status::unsupported) > 0) << "unsupported is not passed, nor failed";
        const auto required = cts::run({.name = "collision-only", .families = {"collision"}}, {}, {.required = {"xpbd"}});
        expect(!required.ok()) << "a required family missing fails";
    };
}};
