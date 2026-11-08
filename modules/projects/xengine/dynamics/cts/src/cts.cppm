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

[[nodiscard]] mlibs::cts::Report run(Subject subject = {}, mlibs::cts::Environment environment = {}, const mlibs::cts::Policy& policy = {});

}  // namespace xengine::dynamics::cts
