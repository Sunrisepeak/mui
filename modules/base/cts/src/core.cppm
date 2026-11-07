// mlibs.cts:core — suites, cases, the three results, the report.
//
//     mlibs::cts::Suite<Device> suite{"mlibs.rhi", "1"};
//     suite.add({.id = "rhi.copy", .contract = "mlibs.rhi@1", .level = Level::exact, .needs = {"copy"},
//                .body = [](Device& d, Check& c) { c.equal(read(d), want, "the bytes"); }});
//     auto report = suite.run(device, [&](std::string_view cap) { return has(device, cap); },
//                             {.required = {"compute"}}, {.subject = "lavapipe"});
//     check(report.ok(), report.text());
//
// A case's result is passed or failed; "unsupported" is the suite's verdict,
// from the capabilities alone: a case whose needs the subject lacks does not
// run. A capability the runner's policy requires is never unsupported: its
// absence fails the case. Unsupported never counts as passed.
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
// person or an agent) needs — a hash, a picture's path, a measured value.
class Check {
public:
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

private:
    std::vector<Failure>                             failures_;
    std::vector<std::pair<std::string, std::string>> evidence_;
};

// One case's result.
struct Outcome {
    std::string              id;
    std::string              contract;
    Level                    level = Level::exact;
    Status                   status = Status::passed;
    std::vector<Failure>     failures;
    std::vector<std::string> missing;  // unsupported, or failed for a required capability: what the subject lacked
    std::vector<std::pair<std::string, std::string>> evidence;
    double                   seconds = 0;
};

// What ran the suite, for the report: the subject (a backend, a device, an
// implementation), the platform and the toolchain.
struct Environment {
    std::string subject;
    std::string platform;
    std::string toolchain;
};

// The runner's policy: the capabilities it requires of the subject.
struct Policy {
    std::vector<std::string> required;
};

struct Report {
    std::string          suite;
    std::string          version;
    Environment          environment;
    std::vector<Outcome> outcomes;

    [[nodiscard]] std::size_t count(Status s) const noexcept;
    // No case failed. (Unsupported cases are listed, never counted as passed.)
    [[nodiscard]] bool ok() const noexcept { return count(Status::failed) == 0; }
    // One line a case, then the totals.
    [[nodiscard]] std::string text() const;
    // The machine-readable report (format mlibs.cts@0.1; a file is named
    // *.cts.omui.json).
    [[nodiscard]] std::string json() const;
};

// A case: a stable id, the contract and its version it checks, its level,
// the capabilities it needs, its body.
template <class Subject>
struct Case {
    std::string                                id;
    std::string                                contract;
    Level                                      level = Level::exact;
    std::vector<std::string>                   needs;
    std::function<void(Subject&, Check&)>      body;
};

// The capabilities a subject has: answers whether it has one.
using Capabilities = std::function<bool(std::string_view)>;

template <class Subject>
class Suite {
public:
    Suite(std::string id, std::string version) : id_(std::move(id)), version_(std::move(version)) {}

    Suite& add(Case<Subject> c) {
        cases_.push_back(std::move(c));
        return *this;
    }
    [[nodiscard]] std::span<const Case<Subject>> cases() const noexcept { return cases_; }
    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& version() const noexcept { return version_; }

    // Every case on `subject`, in order (ids filter: only those listed, all
    // when empty).
    [[nodiscard]] Report run(Subject& subject, const Capabilities& has = {}, const Policy& policy = {}, Environment environment = {},
                             std::span<const std::string> only = {}) const {
        Report r{id_, version_, std::move(environment), {}};
        for (const auto& c : cases_) {
            if (!only.empty() && std::ranges::find(only, c.id) == only.end()) continue;
            Outcome o{c.id, c.contract, c.level};
            for (const auto& need : c.needs)
                if (!has || !has(need)) o.missing.push_back(need);
            const bool required_missing = std::ranges::any_of(o.missing, [&](const std::string& m) { return std::ranges::find(policy.required, m) != policy.required.end(); });
            if (!o.missing.empty()) {
                o.status = required_missing ? Status::failed : Status::unsupported;
                if (required_missing) o.failures.push_back({"", "a capability the runner requires is missing"});
                r.outcomes.push_back(std::move(o));
                continue;
            }
            Check check;
            const auto start = std::chrono::steady_clock::now();
            c.body(subject, check);
            o.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            o.status = check.failed() ? Status::failed : Status::passed;
            o.failures = check.failures();
            o.evidence = check.evidence();
            r.outcomes.push_back(std::move(o));
        }
        return r;
    }

private:
    std::string                 id_;
    std::string                 version_;
    std::vector<Case<Subject>>  cases_;
};

}  // namespace mlibs::cts
