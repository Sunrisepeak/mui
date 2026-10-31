// openmui.cts — CTS level S (spec/conformance/S/README.md): every language
// runs the same scenarios through its own builders and API and must record
// the trace pinned beside each.
//
//     for (const auto& s : cts::scenarios()) {
//         Json trace = cts::run(s, invoke, settle);   // the runner's verbs
//         for (const auto& d : cts::check(s, trace)) fail(d);
//     }
export module openmui.cts;

import std;
import nlohmann.json;

export namespace omui::cts {

using Json = nlohmann::json;

struct Scenario {
    std::string           name;  // the file's stem: `counter`
    std::filesystem::path file;  // spec/conformance/S/counter.scenario.omui.json
    Json                  data;
};

// The directory of the scenarios (spec/conformance/S).
[[nodiscard]] std::filesystem::path directory();

// Every scenario, by name; a file that is not JSON is an error.
[[nodiscard]] std::expected<std::vector<Scenario>, std::string> scenarios();

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

}  // namespace omui::cts
