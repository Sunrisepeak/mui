// omui.bench:harness — how a benchmark's body is measured, and its budgets.
//
// A benchmark is a named body that loops on `State::next()`; each pass of the
// loop is one timed iteration (setup inside the loop goes between `pause()`
// and `resume()`). The measuring is mtest's (`mcpplibs.mtest.bench`'s `laps`:
// each call timed by itself, less what it paused for, with the quantities the
// body states and the allocations the program counts); `State` is the scene's
// side of it, and `run_one` gives what one benchmark measured under the names
// its budgets use. Running them, keeping the numbers and judging the budgets
// is the test runner's (tests/budgets_test.cpp: a case a benchmark, its
// budgets the case's bounds).
//
//     omui::bench::add("frame/one-label", "…", [](omui::bench::State& s) {
//         while (s.next()) step();
//     });
export module omui.bench:harness;

import std;

export namespace omui::bench {

struct Options {
    double       min_time_ms = 400;      // per benchmark, after the warm-up
    std::size_t  min_iterations = 12;
    std::size_t  max_iterations = 100'000;
    std::size_t  warmup = 3;
};

// The loop a benchmark's body runs: `while (state.next()) { … }`.
class State {
public:
    explicit State(const Options& options);
    ~State();
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    // Ends the iteration that was running (if any) and starts the next one;
    // false when what the options ask has been measured.
    [[nodiscard]] bool next();

    // Time between pause() and resume() is not part of the iteration.
    void pause() noexcept;
    void resume() noexcept;

    // The iteration did `n` things (spans, glyphs, nodes): the report gives the cost of one.
    void items(std::uint64_t n) noexcept;

    // A figure the body measured itself, reported as the mean over the timed iterations.
    void counter(std::string_view name, double value);

    // A fact about the run that is not a number (the GPU it ran on), in the case's output.
    void note(std::string_view name, std::string text);

    // The benchmark cannot run here (no GPU, say): its case is skipped, with
    // the reason, and never numbers; its budgets do not apply. The body
    // returns after it.
    void skip(std::string reason);
    // What it measures did not happen (a GPU frame that failed): its case
    // fails, never numbers. The body returns after it.
    void fail(std::string reason);

    // Iterations ended, the warm-up's counted.
    [[nodiscard]] std::size_t iteration() const noexcept;

private:
    friend struct Access;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct Result {
    std::string  name;
    std::string  description;
    std::size_t  iterations = 0;
    // Where the iterations lie, nearest rank.
    double       min_ms = 0, mean_ms = 0, p50_ms = 0, p95_ms = 0, p99_ms = 0, max_ms = 0;
    std::uint64_t items = 0;             // per iteration; 0: the body counted none
    double       ns_per_item = 0;        // mean / items
    double       allocations = -1;       // per iteration (mean), of the thread that ran it; -1: the program counts none
    std::uint64_t max_allocations = 0;
    std::vector<std::pair<std::string, double>> counters;
    std::vector<double> samples_ms;      // each measured iteration, in the order run
    std::string  skipped;                // why it did not run; empty when it did
    std::string  failed;                 // why what it measures went wrong; empty when it did not
    std::vector<std::pair<std::string, std::string>> notes;

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
    // Not applied to a run that counted this (non-zero): "software", for a
    // GPU's frame budget that a software device (lavapipe) cannot speak to.
    std::string unless;
};

using Body = std::function<void(State&)>;

struct Case {
    std::string name;
    std::string description;
    Body        body;
};

void add(std::string name, std::string description, Body body);
[[nodiscard]] std::span<const Case> cases() noexcept;

// One benchmark measured: inside a test's case the measurement is also the
// case's metrics (mtest's `iteration`, `iteration.p99`, …).
[[nodiscard]] Result run_one(const Case& c, const Options& options);

// budgets.bench.omui.json: {"format":"openmui.bench.budgets@0.1","budgets":[{"name":…,"metric":…,"max":…,"why":…,"unless":…}]}
[[nodiscard]] std::expected<std::vector<Budget>, std::string> parse_budgets(std::string_view json);

}  // namespace omui::bench
