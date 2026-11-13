// The benchmarks as mtest's cases (the foundation plan's §6.13.1, T2.12): each benchmark is a case of kind
// `bench` that reports what it measured as the case's metrics — the time of an iteration by where the
// iterations lie (p50, p95, p99, max), its allocations, its own counters — and whose budgets
// (budgets.bench.omui.json) are the bounds on those metrics: the runner judges them at the case's end, keeps the
// numbers with the run, and compares them with the runs before (`mtest analyze trend`, `[policy.bench]`).
//
//   OMUI_BENCH=1 mcpp test -p omui.bench --profile release budgets
//
// A budget says what a release build may cost on a machine nothing else uses, so the cases run only where that
// is said (OMUI_BENCH); elsewhere they are skipped with the reason. `OMUI_BENCH=quick` runs a pass of each and
// judges nothing — that every benchmark still runs and reports.
//
// Heap allocations are counted by this program's own `operator new` (a file of its own program for that reason):
// the libraries measured are the ones every application links.
import std;
import omui.bench;
import mcpplibs.os;
import mcpplibs.mtest;
import mcpplibs.mtest.bench;

void* operator new(std::size_t size) {
    omui::bench::count_allocation();
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    std::abort();  // core code uses no exceptions; running out of memory ends the run
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

using namespace mcpplibs::mtest;
namespace harness = omui::bench;

namespace {

// A benchmark's budgets as bounds on its case: an attribute of this file's.
struct Budgets {
    std::vector<metric_bound> bounds;
};
void apply(attributes& set, Budgets budgets) {
    for (metric_bound& bound : budgets.bounds) set.bounds.push_back(std::move(bound));
}

std::vector<harness::Budget> budgets_in_the_tree() {
    const auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "budgets.bench.omui.json";
    std::ifstream in(file, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    auto read = harness::parse_budgets(text);
    return read ? std::move(*read) : std::vector<harness::Budget>{};
}

// What a result reports, each under the name its budget uses.
void report(const harness::Result& result) {
    std::vector<double> seconds;
    seconds.reserve(result.samples_ms.size());
    for (const double sample : result.samples_ms) seconds.push_back(sample / 1000.0);
    const auto lie = mcpplibs::mtest::bench::distribute(std::move(seconds), result.items);
    metric("min_ms", lie.min * 1000.0, "ms");
    metric("mean_ms", lie.mean * 1000.0, "ms");
    metric("p50_ms", lie.p50 * 1000.0, "ms");
    metric("p95_ms", lie.p95 * 1000.0, "ms");
    metric("p99_ms", lie.p99 * 1000.0, "ms");
    metric("max_ms", lie.max * 1000.0, "ms");
    if (result.items) metric("ns_per_item", lie.per_item * 1e9, "ns");
    if (result.allocations >= 0) {
        metric("allocations", result.allocations, "1");
        metric("max_allocations", static_cast<double>(result.max_allocations), "1");
    }
    for (const auto& [name, value] : result.counters) metric(name, value, "1");
}

}  // namespace

Bench budgets{"bench.budgets", resource{"machine"}, [] {
    const std::string asked = mcpplibs::os::env::get("OMUI_BENCH").value_or("");
    const bool quick = asked == "quick";
    harness::add_framework_benchmarks();
    const std::vector<harness::Budget> budgets = budgets_in_the_tree();
    expect(!budgets.empty()) << "budgets.bench.omui.json reads";
    for (const harness::Budget& budget : budgets)
        expect(std::ranges::any_of(harness::cases(), [&](const harness::Case& one) { return one.name == budget.name; })) << "a budget for no benchmark: " + budget.name;

    for (const harness::Case& one : harness::cases()) {
        // A budget with `unless` is not a bound of every run (a GPU's frame budget on a software device): it is
        // judged in the case, where the run's counters are known. The others are the case's bounds.
        Budgets bounds;
        std::vector<harness::Budget> conditional;
        for (const harness::Budget& budget : budgets) {
            if (budget.name != one.name || quick) continue;
            if (budget.unless.empty()) bounds.bounds.push_back({budget.metric, budget.max, std::nullopt});
            else conditional.push_back(budget);
        }
        test(one.name)[skip_if{asked.empty(), "a budget is of a release build on a machine nothing else uses: OMUI_BENCH=1 (or quick: one pass, nothing judged)"}][std::move(bounds)] =
            [one, conditional, quick] {
                const harness::Result result = harness::run_one(one, quick ? harness::Options{.min_time_ms = 0, .min_iterations = 1, .max_iterations = 1, .warmup = 0} : harness::Options{});
                for (const auto& [name, text] : result.notes) std::println("{}: {}", name, text);
                if (!result.skipped.empty()) skip(result.skipped);
                expect(fatal(result.failed.empty())) << result.failed;
                report(result);
                for (const harness::Budget& budget : conditional) {
                    if (result.metric(budget.unless).value_or(0) != 0) {
                        std::println("budget {} ≤ {} not applied: this run counted `{}`", budget.metric, budget.max, budget.unless);
                        continue;
                    }
                    const auto value = result.metric(budget.metric);
                    expect(value.has_value() && le(*value, budget.max)) << std::format("{} is {} — at most {}: {}", budget.metric, value.value_or(std::numeric_limits<double>::quiet_NaN()), budget.max, budget.why);
                }
            };
    }
}};
