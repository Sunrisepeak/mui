// mlibs.agent.cts — the agent contract as cases (D-174; the plan's §3.3): the
// eight native properties, checked on a subject through the same session an
// agent has. The suite knows no subject's operations: its owner hands it a
// probe — how one is made, and which of its requests show each property.
//
//     import mlibs.agent.cts;
//     namespace contract = mlibs::agent::cts;
//
//     const contract::Probe probe{
//         .make = [] { return std::make_unique<Game>(first_state()); },
//         .read = R"({"op":"world.observe"})",
//         .change = R"({"op":"world.step","steps":3})",
//         .preview = R"({"op":"world.branch","steps":30})",
//         .script = {R"({"op":"world.act","axis":[1]})", R"({"op":"world.step","steps":40})"},
//     };
//     const auto report = contract::run(probe, {.subject = "xengine.runtime"});
//     expect(report.ok()) << report.text();
//
// What a subject says it cannot do (its capabilities) is unsupported, never
// passed; what it says it can and its probe does not show fails.
export module mlibs.agent.cts;

import std;
export import mlibs.cts;
export import mlibs.agent;

export namespace mlibs::agent::cts {

// The contract the cases check, and its version.
inline constexpr std::string_view contract = "mlibs.agent.contract@1";

struct Probe {
    // A new subject, in the same first state each time (two are compared).
    std::function<std::unique_ptr<Subject>()> make;
    // Requests (one JSON line each) that show a property. One left empty is not shown: that fails the
    // cases of a capability the subject says it has.
    std::string read;                 // reads its state (observation)
    std::string change;               // changes it (control)
    std::string undo;                 // takes `change` back (history)
    std::string preview;              // what would be, the state untouched (preview)
    std::vector<std::string> script;  // plays it on: the same requests, the same bits (time)
    std::string explain;              // why the state `change` left is what it is (causes)
    std::string source;               // from something it put out to what made it (provenance)
    std::string budgeted;             // `read` asked within a budget (budget) …
    std::size_t budget = 0;           // … of this many bytes of answer
};

// The capabilities a case may need, as Capabilities names them.
[[nodiscard]] bool has(const Capabilities& capabilities, std::string_view capability) noexcept;

[[nodiscard]] const mlibs::cts::Suite<const Probe>& suite();
// Every case on the probe's subject. `environment.subject` names it in the report.
[[nodiscard]] mlibs::cts::Report run(const Probe& probe, mlibs::cts::Environment environment = {}, const mlibs::cts::Policy& policy = {});

}  // namespace mlibs::agent::cts
