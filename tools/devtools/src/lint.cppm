// omui.devtools.lint — the repository's rules, checked (AGENTS.md; D-206: the
// lint in C++, a subcommand of the contributors' tool). Each rule reads the
// tree as it is and answers problems: a rule's name, where, and what to do.
// `self_test` plants each rule's violation in a small tree of its own and
// shows the rule catches it and nothing else does.
//
// The rules move here from tools/lint/lint.py group by group (RC3.12); until
// the last one has moved, the script runs the rest.
export module omui.devtools.lint;

import std;

export namespace omui::devtools::lint {

struct Problem {
    std::string rule;     // "macros", "layering", "owner" …
    std::string where;    // a path from the root, with ":line" for a source
    std::string message;  // what is wrong and what to do

    [[nodiscard]] std::string text() const;  // "[rule] where: message"
    bool operator==(const Problem&) const = default;
};

// A rule: its name and the problems it finds under a root.
struct Rule {
    std::string_view name;
    std::string_view summary;  // one line, for --list
};

// The rules this module checks, in the order they run.
[[nodiscard]] std::span<const Rule> rules() noexcept;

// Every rule (or the ones named) over the tree under `root`.
[[nodiscard]] std::vector<Problem> run(const std::filesystem::path& root, std::span<const std::string> only = {});

// Removes // and /* */ comments and the contents of string and character
// literals (raw strings too), keeping every line where it was.
[[nodiscard]] std::string strip_comments(std::string_view text);

// Each rule's violation, planted in a fresh tree under `scratch`: whether the
// rule caught it and no other rule complained. The repository at `root` gives
// the tables the rules read (the owners' samples). Lines for a person on
// `out`; the number of rules that failed.
int self_test(const std::filesystem::path& root, const std::filesystem::path& scratch, std::ostream& out);

}  // namespace omui::devtools::lint
