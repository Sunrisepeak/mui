// omui.devtools.check — CI's acceptance steps (moved from tools/ci/*.sh, D-206). Each builds what it needs
// through mcpp (its products from mcpp's own record), runs it and holds the result; it says `ok …` for each
// thing it showed, `::error::…` for what failed, and returns the step's exit status. A workflow's step is the one
// line that calls it (the allowlist's first kind).
//
// features  M0 acceptance ⑤ (plan §19.2): a feature selects one of two mutually exclusive source files. Each
//           choice builds and passes its test — tests/features with its default `sink-stdout` (a workspace member),
//           and an application asking for `sink-memory` (tools/ci/features-consumer, built standalone: a workspace
//           build plans every member together and would give the fixture both) — and asking for both is refused
//           before anything compiles: both files provide the partition `omui.features:sink`.
// size      where a page's WebAssembly bytes go (W8, Q-38; the foundation plan's M6.3 budget): a package built for
//           wasm32-emscripten as a release is (-O2, the wasm-size profile: its functions' names kept, left out of
//           the count), `omui-gen wasm-size` over it — sections, code by owner, largest functions — and, given a
//           budget (<name>.budget.omui.json), its bytes held to it.
// atspi     examples 03 and 07 built and read and operated through AT-SPI (`omui-devtools atspi`) on a private
//           session bus (dbus-run-session), as a screen reader does. Linux x86_64 (AccessKit's library);
//           at-spi2-core.
export module omui.devtools.check;

import std;

export namespace omui::devtools::check {

struct Size {
    std::string package;             // e.g. omui.site
    std::filesystem::path json;      // the report (default: target/<package>.wasm-size.json)
    std::filesystem::path markdown;  // and as a table, when asked
    std::filesystem::path budget;    // the budgets, when it has them
};

[[nodiscard]] int features(const std::filesystem::path& root);
[[nodiscard]] int size(const std::filesystem::path& root, const Size& asked);
[[nodiscard]] int atspi(const std::filesystem::path& root, const std::vector<std::string>& profile_args);

// The measured bytes held to a budget file's {"budgets": [{"metric", "max"}]}: each one over it, as a message.
[[nodiscard]] std::vector<std::string> over_budget(const std::map<std::string, std::uint64_t>& measured, std::string_view budget_text);

}  // namespace omui::devtools::check
