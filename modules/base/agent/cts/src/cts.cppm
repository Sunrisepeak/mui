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
//     contract::declare(probe, {.subject = "xengine.runtime"});   // mtest's cases: xengine.runtime/agent.read.pure, …
//     expect(contract::unsupported(game.capabilities()) == std::vector<std::string>{"agent.source.chain"});
//
// What a subject says it cannot do (its capabilities) is unsupported — skipped
// with the capability it lacks — never passed; what it says it can and its
// probe does not show fails.
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

// Every case against the probe's subject: mtest's conformance cases `<subject>/agent.<case>` (in a tally,
// counted). `who.subject` names it (its own name when none is given); what it has is its own capabilities.
void declare(const Probe& probe, mlibs::cts::Against who = {}, std::source_location at = std::source_location::current());

// The cases a subject with these capabilities leaves unsupported, in the suite's order: what a subject's
// tests pin, so that a capability given up is seen.
[[nodiscard]] std::vector<std::string> unsupported(const Capabilities& capabilities);
// How many cases the contract has.
[[nodiscard]] std::size_t case_count() noexcept;

}  // namespace mlibs::agent::cts
