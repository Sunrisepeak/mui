// The dynamics conformance on the CPU reference: every case passes; a
// subject without a family has that family's cases unsupported (never
// passed), and failed when the runner requires it; the report as JSON.
import std;
import xengine.dynamics.cts;
import mlibs.testing;

using namespace mlibs::testing;
namespace cts = xengine::dynamics::cts;

int main(int argc, char** argv) {
    Suite s{"dynamics.cts"};
    s.test("the CPU reference passes every case", [] {
        const auto report = cts::run();
        std::println("{}", report.text());
        check(report.ok() && report.count(mlibs::cts::Status::passed) == report.outcomes.size() && report.outcomes.size() >= 6, report.text());
        check(report.json().contains("\"suite\""), "a machine-readable report");
    });
    s.test("a subject without a family: its cases unsupported, failed when required", [] {
        const auto partial = cts::run({.name = "collision-only", .families = {"collision"}});
        check(partial.ok() && partial.count(mlibs::cts::Status::unsupported) > 0, "unsupported is not passed, nor failed");
        const auto required = cts::run({.name = "collision-only", .families = {"collision"}}, {}, {.required = {"xpbd"}});
        check(!required.ok(), "a required family missing fails");
    });
    return s.run(argc, argv);
}
