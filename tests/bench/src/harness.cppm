// openmui.bench:harness — what runs a benchmark and reports it.
//
// A benchmark is a named body that loops on `State::next()`; each pass of the
// loop is one timed iteration (setup inside the loop goes between `pause()`
// and `resume()`). The harness warms up, iterates until it has both enough
// iterations and enough time, and reports per iteration: min, mean, p50,
// p95, p99 and max (nearest rank), the cost per item when the body counts
// items, heap allocations per iteration (when the program counts them), and
// the body's own counters (node counts, phase times).
//
// A report is JSON (`openmui.bench@0.1`, written as `<name>.bench.omui.json`)
// and a table; budgets (`budgets.bench.omui.json`) turn it into a gate: each
// names a benchmark, a metric and its limit, and says why.
//
//     omui::bench::add("frame/one-label", "…", [](omui::bench::State& s) {
//         while (s.next()) step();
//     });
//     auto report = omui::bench::run({.filter = "frame/"});
//     std::print("{}", report.table());
export module openmui.bench:harness;

import std;

export namespace omui::bench {

struct Options {
    std::string  filter;                 // run the benchmarks whose name contains it (all when empty)
    double       min_time_ms = 400;      // per benchmark, after the warm-up
    std::size_t  min_iterations = 12;
    std::size_t  max_iterations = 100'000;
    std::size_t  warmup = 3;
    std::string  label;                  // what was measured: the toolchain and profile, a machine name
};

// The loop a benchmark's body runs: `while (state.next()) { … }`.
class State {
public:
    // Ends the iteration that was running (if any) and starts the next one;
    // false when the harness has what it needs.
    [[nodiscard]] bool next();

    // Time between pause() and resume() is not part of the iteration.
    void pause() noexcept;
    void resume() noexcept;

    // The iteration did `n` things (spans, glyphs, nodes): the report gives the cost of one.
    void items(std::uint64_t n) noexcept { items_ = n; }

    // A figure the body measured itself, reported as the mean over the timed iterations.
    void counter(std::string_view name, double value);

    [[nodiscard]] std::size_t iteration() const noexcept { return done_; }

    struct Config {
        std::size_t warmup, min_iterations, max_iterations;
        double      min_time_ms;
    };
    explicit State(Config config) : config_(config) {}

private:
    friend struct Access;
    using clock = std::chrono::steady_clock;
    Config                                      config_;
    bool                                        running_ = false;
    std::size_t                                 done_ = 0;       // iterations ended, warm-up included
    clock::time_point                           started_{};
    clock::time_point                           paused_at_{};
    clock::duration                             paused_{};
    clock::duration                             measured_total_{};
    std::uint64_t                               allocs_at_ = 0;
    std::uint64_t                               allocs_paused_ = 0;
    std::uint64_t                               allocs_pause_at_ = 0;
    std::uint64_t                               items_ = 0;
    std::vector<double>                         samples_ms_;
    std::vector<std::uint64_t>                  allocations_;
    std::vector<std::pair<std::string, double>> counters_;       // sums over the timed iterations
    std::vector<std::pair<std::string, std::size_t>> counter_n_;
};

struct Result {
    std::string  name;
    std::string  description;
    std::size_t  iterations = 0;
    double       min_ms = 0, mean_ms = 0, p50_ms = 0, p95_ms = 0, p99_ms = 0, max_ms = 0;
    std::uint64_t items = 0;             // per iteration; 0: the body counted none
    double       ns_per_item = 0;        // mean / items
    double       allocations = -1;       // per iteration (mean); -1: not counted
    std::uint64_t max_allocations = 0;
    std::vector<std::pair<std::string, double>> counters;

    // A metric by name: min_ms mean_ms p50_ms p95_ms p99_ms max_ms ns_per_item
    // allocations max_allocations, or a counter's name.
    [[nodiscard]] std::optional<double> metric(std::string_view name) const;
};

// A limit on one metric of one benchmark, and the reason it is there.
struct Budget {
    std::string name;
    std::string metric = "p95_ms";
    double      max = 0;
    std::string why;
};

struct Violation {
    Budget budget;
    double value = 0;                    // NaN: the benchmark or the metric was not in the report
};

struct Report {
    std::string          label;
    std::string          platform;
    unsigned             hardware_threads = 0;
    std::string          started;        // UTC, ISO 8601
    std::vector<Result>  results;

    [[nodiscard]] const Result* find(std::string_view name) const noexcept;
    [[nodiscard]] std::string json() const;
    [[nodiscard]] std::string table() const;
    // Budgets of benchmarks that did not run are skipped unless `require_all`.
    [[nodiscard]] std::vector<Violation> check(std::span<const Budget> budgets, bool require_all = false) const;
};

using Body = std::function<void(State&)>;

struct Case {
    std::string name;
    std::string description;
    Body        body;
};

void add(std::string name, std::string description, Body body);
[[nodiscard]] std::span<const Case> cases() noexcept;

[[nodiscard]] Report run(const Options& options);
[[nodiscard]] Result run_one(const Case& c, const Options& options);

// budgets.bench.omui.json: {"format":"openmui.bench.budgets@0.1","budgets":[{"name":…,"metric":…,"max":…,"why":…}]}
[[nodiscard]] std::expected<std::vector<Budget>, std::string> parse_budgets(std::string_view json);

// Heap allocations: a program that replaces the global operator new calls
// count_allocation() from it; until one does, allocations are not reported.
void count_allocation() noexcept;
[[nodiscard]] std::uint64_t allocations() noexcept;
[[nodiscard]] bool counting_allocations() noexcept;

}  // namespace omui::bench
