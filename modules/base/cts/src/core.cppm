// mlibs.cts:core — a suite's cases declared as mtest's conformance cases, what
// a case records, and the cases counted in a process that is not a test.
//
// A suite is a library of its contract's owner: a function that declares its
// cases against the subject it is given.
//
//     void declare(Device& device, mlibs::cts::Against who) {
//         who.has = [&](std::string_view capability) { return has(device, capability); };
//         mlibs::cts::against(std::move(who), [&] {
//             mlibs::cts::declare({.id = "rhi.copy", .contract = "mlibs.rhi@1", .level = Level::exact, .needs = {"copy"},
//                                  .body = [&](Check& c) { c.equal(read(device), want, "the bytes"); }});
//         });
//     }
//
// In a test program each is a case of mtest (mcpplibs.mtest.cts): named
// `<subject>/<id>`, of the kind `cts`, covering its contract as its clause,
// tagged `level:S|A|B`, and — where the subject lacks what it needs — skipped as
// `unsupported: <capability>`, never passed. What the runner requires and the
// subject lacks fails once (`<subject>/requires <capability>`). The runner, the
// selection and the reports are mtest's.
//
// A program that is not a test (a tool answering which cases hold on this
// device) counts the same cases itself: `tally`.
export module mlibs.cts:core;

import std;

export namespace mlibs::cts {

// The level a case checks (the specification's conformance levels): S the
// semantics (structure, records), A identical bits, B within a tolerance the
// case declares.
enum class Level : std::uint8_t { semantic, exact, tolerance };
enum class Status : std::uint8_t { passed, failed, unsupported };

[[nodiscard]] constexpr std::string_view name(Level l) noexcept {
    switch (l) {
        case Level::semantic: return "S";
        case Level::exact: return "A";
        case Level::tolerance: return "B";
    }
    return "?";
}
[[nodiscard]] constexpr std::string_view name(Status s) noexcept {
    switch (s) {
        case Status::passed: return "passed";
        case Status::failed: return "failed";
        case Status::unsupported: return "unsupported";
    }
    return "?";
}

// Where a case went wrong, and why.
struct Failure {
    std::string where;  // file:line of the check
    std::string why;
};

// What a case's body records: its failures, and the evidence a reviewer (a
// person or an agent) needs — a hash, a picture's path, a measured value. In a
// test program a failure is mtest's, at the check's place, and evidence is a
// line of the case's output (`evidence <kind>: <value>`); both are also kept
// here, for the body to ask and for a tally.
class Check {
public:
    Check() = default;
    // Records a failure unless `ok`.
    bool that(bool ok, std::string_view why, std::source_location at = std::source_location::current());
    // Equal values; the failure says both.
    template <class T>
    bool equal(const T& got, const T& want, std::string_view what, std::source_location at = std::source_location::current()) {
        if (got == want) return true;
        if constexpr (std::formattable<T, char>) return that(false, std::format("{}: expected {}, got {}", what, want, got), at);
        else return that(false, std::format("{}: not the expected value", what), at);
    }
    void evidence(std::string_view kind, std::string_view value);

    [[nodiscard]] bool failed() const noexcept { return !failures_.empty(); }
    [[nodiscard]] const std::vector<Failure>& failures() const noexcept { return failures_; }
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& evidence() const noexcept { return evidence_; }

    // A check whose failures and evidence are also the running case's (mtest's).
    [[nodiscard]] static Check reported() {
        Check check;
        check.reported_ = true;
        return check;
    }

private:
    bool                                             reported_ = false;
    std::vector<Failure>                             failures_;
    std::vector<std::pair<std::string, std::string>> evidence_;
};

// A case: a stable id, the contract and its version it checks, its level,
// the capabilities it needs of the subject, its body.
struct Case {
    std::string                 id;
    std::string                 contract;
    Level                       level = Level::exact;
    std::vector<std::string>    needs;
    std::function<void(Check&)> body;
};

// What a suite is run against: the subject (a backend, a device, an
// implementation) as the runner names it, whether it has a capability (none
// given: it has every one — a suite that knows its subject fills this in), and
// the capabilities this run requires of it.
struct Against {
    std::string                           subject;
    std::function<bool(std::string_view)> has;
    std::vector<std::string>              required;
};

// Runs a suite against `who`: `cases` declares them (`declare`).
void against(Against who, const std::function<void()>& cases, std::source_location at = std::source_location::current());
// One case of the suite being run.
void declare(Case c, std::source_location at = std::source_location::current());

// The cases counted in this process, for a program that is not a test.
struct Tally {
    struct Entry {
        std::string              subject;
        std::string              id;
        std::string              contract;
        Level                    level = Level::exact;
        Status                   status = Status::passed;
        std::vector<Failure>     failures;
        std::vector<std::string> missing;  // unsupported: what the subject lacked
        std::vector<std::pair<std::string, std::string>> evidence;
    };
    std::vector<Entry> cases;

    [[nodiscard]] std::size_t count(Status s) const noexcept;
    // No case failed. (Unsupported cases are listed, never counted as passed.)
    [[nodiscard]] bool ok() const noexcept { return count(Status::failed) == 0; }
    // The ids of the cases that ended so, in order.
    [[nodiscard]] std::vector<std::string> ids(Status s) const;
    [[nodiscard]] const Entry* find(std::string_view id) const noexcept;
    // One line a case, then the totals.
    [[nodiscard]] std::string text() const;
};
// Every case `suites` declares (through `against`), run here and counted: no
// case of mtest is declared, nothing is reported but what is returned.
[[nodiscard]] Tally tally(const std::function<void()>& suites);

}  // namespace mlibs::cts
