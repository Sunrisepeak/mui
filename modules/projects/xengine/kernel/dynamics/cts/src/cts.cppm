// xengine.dynamics.cts — every solver family's cases (contract ids
// xengine.dynamics.collision@1, .xpbd@1, .material@1) on a subject. The CPU
// reference is the subject today; a SIMD or GPU tier will be another subject
// held to the same cases (within its declared tolerance), and a family it
// does not implement is unsupported, never passed.
export module xengine.dynamics.cts;

import std;
export import mlibs.cts;

export namespace xengine::dynamics::cts {

// Which implementation runs the cases, and what it has ("collision", "xpbd", "material").
struct Subject {
    std::string            name = "cpu-reference";
    std::vector<std::string> families{"collision", "xpbd", "material"};
};

// Every case against `subject`: mtest's conformance cases `<subject>/<family>.<case>` (in a tally, counted). What
// it has is its families; `who` may name it otherwise and say what the run requires.
void declare(Subject subject = {}, mlibs::cts::Against who = {}, std::source_location at = std::source_location::current());
// How many cases the suite has.
[[nodiscard]] std::size_t case_count() noexcept;

}  // namespace xengine::dynamics::cts
