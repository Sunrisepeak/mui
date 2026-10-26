// openmui.testing — the checks every openmui test program uses.
//
// mcpp's contract for a test is an exit code, so this is not a framework: a
// suite is a list of named cases, a failed check records where it happened and
// the case goes on, and `run()` turns the record into the exit code. Nothing
// here is a macro; the location of a check comes from `std::source_location`.
export module openmui.testing;

import std;

export namespace omui::testing {

// A named group of cases. Cases run in the order they were added; a case that
// fails a check keeps running, so one report lists every broken expectation.
class Suite {
public:
    explicit Suite(std::string name);

    Suite& test(std::string name, std::function<void()> body);

    // Runs every case whose name contains `filter` (all when empty); the result
    // is the program's exit code: 0 when every check held.
    [[nodiscard]] int run(std::string_view filter = {});

    // Arguments after `mcpp test -- …` select cases by substring.
    [[nodiscard]] int run(int argc, char** argv);

private:
    struct Case {
        std::string           name;
        std::function<void()> body;
    };
    std::string       name_;
    std::vector<Case> cases_;
};

// Records a failure in the running case when `ok` is false.
void check(bool ok, std::string_view what = {},
           std::source_location where = std::source_location::current());

// Records a failure that has a message and no condition.
void fail(std::string_view what, std::source_location where = std::source_location::current());

// Exact comparison of two doubles by bit pattern, so a determinism check cannot
// be satisfied by a value that only compares equal (+0 / -0).
void check_bits(double actual, double expected,
                std::source_location where = std::source_location::current());

void check_near(double actual, double expected, double tolerance,
                std::source_location where = std::source_location::current());

template <class A, class B>
void check_eq(const A& actual, const B& expected,
              std::source_location where = std::source_location::current()) {
    if (actual == expected) {
        check(true, {}, where);
        return;
    }
    if constexpr (std::formattable<A, char> && std::formattable<B, char>) {
        fail(std::format("expected {}, got {}", expected, actual), where);
    } else {
        fail("values differ", where);
    }
}

// The number of failed checks in the running case; a case can stop early when
// a precondition for the rest of it did not hold.
[[nodiscard]] int failures() noexcept;

// FNV-1a over bytes, for the pinned hashes of determinism tests: one value per
// sampled table keeps the tests short and makes a cross-platform difference a
// single, comparable line in the CI log.
class Hash {
public:
    void bytes(std::span<const std::byte> data) noexcept;
    void f64(double v) noexcept;
    void f32(float v) noexcept;
    void u64(std::uint64_t v) noexcept;
    void str(std::string_view s) noexcept;
    [[nodiscard]] std::uint64_t value() const noexcept { return h_; }
    [[nodiscard]] std::string hex() const;

private:
    std::uint64_t h_ = 0xcbf29ce484222325ull;
};

// A directory a test may write into, removed when the program starts again.
[[nodiscard]] std::filesystem::path scratch_dir(std::string_view test_name);

}  // namespace omui::testing
