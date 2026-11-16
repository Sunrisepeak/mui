// openmui.cts — CTS level S (spec/conformance/S/README.md): every language
// runs the same scenarios through its own builders and API and must record
// the trace pinned beside each.
//
//     const auto report = cts::conform([&](const cts::Scenario& s) {
//         return cts::run(s, invoke, settle);   // the language runner's verbs
//     }, {.subject = "ts"});
//     check(report && report->ok(), report ? report->text() : report.error());
export module omui.cts;

import std;
import nlohmann.json;
import mlibs.error;
export import mlibs.cts;

export namespace omui::cts {

using Json = nlohmann::json;

// Why the scenarios cannot be read (mlibs.error).
enum class CtsError : std::uint8_t { unreadable, not_a_scenario, unlistable, no_scenario };
[[nodiscard]] constexpr mlibs::error::Description describe(CtsError error) noexcept {
    using Kind = mlibs::error::Kind;
    switch (error) {
        case CtsError::unreadable: return {"omui.cts.unreadable", Kind::not_found, "a scenario file cannot be read"};
        case CtsError::not_a_scenario: return {"omui.cts.not-a-scenario", Kind::invalid_argument, "a file named as a scenario is not one"};
        case CtsError::unlistable: return {"omui.cts.unlistable", Kind::not_found, "the scenarios' directory cannot be listed"};
        case CtsError::no_scenario: return {"omui.cts.no-scenario", Kind::not_found, "no scenario to run"};
    }
    return {"omui.cts.?", Kind::internal};
}
using CtsFailure = mlibs::error::Error<CtsError>;

struct Scenario {
    std::string           name;  // the file's stem: `counter`
    std::filesystem::path file;  // spec/conformance/S/counter.scenario.omui.json
    Json                  data;
};

// The directory of the scenarios (spec/conformance/S).
[[nodiscard]] std::filesystem::path directory();

// Every scenario, by name; a file that is not JSON is an error.
[[nodiscard]] std::expected<std::vector<Scenario>, CtsFailure> scenarios();

// A script runner's trace. The runner registered the verbs `cts.load` (the
// scenario) and `cts.step` ({"step": i}); `invoke` calls one and answers its
// data (or {"runner-error": message}), `settle` runs frames until the scripts
// have nothing left to do.
[[nodiscard]] Json run(const Scenario& scenario, const std::function<Json(std::string_view verb, const Json& args)>& invoke,
                       const std::function<void()>& settle);

// A trace as it is compared: trees without `bounds`, an empty list and an
// empty map alike.
[[nodiscard]] Json canonical(const Json& trace);

// What is wrong with a trace: what the steps' `expect` says that it does not
// show, then where it differs from the pinned trace (the first differing
// record, with both). Empty when nothing is. With OMUI_CTS_WRITE=1 the pinned
// trace is written instead of compared.
[[nodiscard]] std::vector<std::string> check(const Scenario& scenario, const Json& trace);

// The suite of level S (mlibs.cts), against a language's runner: one case a scenario (`openmui.S.<name>`,
// contract openmui.conformance.S@0.1) — mtest's conformance cases `<subject>/openmui.S.<name>` — whose body is
// `trace_of(scenario)` checked against the pinned trace. How many scenarios there are, or why there are none.
std::expected<std::size_t, CtsFailure> conform(const std::function<Json(const Scenario&)>& trace_of, mlibs::cts::Against who = {},
                                                std::source_location at = std::source_location::current());

}  // namespace omui::cts
