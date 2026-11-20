// omui.devtools.lint — the repository's rules, checked (AGENTS.md; D-206: the
// lint in C++, a subcommand of the contributors' tool). Each rule reads the
// tree as it is and answers problems: a rule's name, where, and what to do.
// `self_test` plants each rule's violation in a small tree of its own and
// shows the rule catches it and nothing else does. `omui-devtools lint` runs
// them in CI's lint row and in omui-devtools ci local; devtools.lint holds the
// repository to them in every test row.
export module omui.devtools.lint;

import std;
export import :problem;

export namespace omui::devtools::lint {

// The rules this module checks, in the order they run.
[[nodiscard]] std::span<const Rule> rules() noexcept;

// Every rule (or the ones named) over the tree under `root`.
[[nodiscard]] std::vector<Problem> run(const std::filesystem::path& root, std::span<const std::string> only = {});

// The problems of the rules named (all of them when none is): what `run` keeps.
[[nodiscard]] std::vector<Problem> select(std::vector<Problem> problems, std::span<const std::string> only);

// Removes // and /* */ comments and the contents of string and character
// literals (raw strings too), keeping every line where it was.
[[nodiscard]] std::string strip_comments(std::string_view text);

// Each rule's violation, planted in a fresh tree under `scratch`: whether the
// rule caught it and no other rule complained. The repository at `root` gives
// the tables the rules read (the owners' samples). Lines for a person on
// `out`; the number of rules that failed.
int self_test(const std::filesystem::path& root, const std::filesystem::path& scratch, std::ostream& out);

}  // namespace omui::devtools::lint
