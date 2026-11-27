// What a rule answers (omui.devtools.lint): a problem, and a rule's name and summary.
export module omui.devtools.lint:problem;

import std;

export namespace omui::devtools::lint {

struct Problem {
    std::string rule;     // "macros", "layering", "owner" …
    std::string where;    // a path from the root, with ":line" for a source
    std::string message;  // what is wrong and what to do

    [[nodiscard]] std::string text() const { return std::format("[{}] {}: {}", rule, where, message); }
    bool operator==(const Problem&) const = default;
};

// A rule: its name and one line for --list.
struct Rule {
    std::string_view name;
    std::string_view summary;
};

}  // namespace omui::devtools::lint
