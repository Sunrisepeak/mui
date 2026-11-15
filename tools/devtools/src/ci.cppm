// omui.devtools.ci — a CI run read for the people and agents who reconcile it
// (R12; release design RC0.2): each row, each failing member, program and
// case with its first lines, set against STATUS's red list.
export module omui.devtools.ci;

import std;

export namespace omui::devtools::ci {

// One program that failed, or a member whose package did not build.
struct Failure {
    std::string program;             // "nav_test"; empty when the package did not build
    std::string why;                 // "exit 1, 0.96s, 13 cases", "timeout after 300s", "did not build"
    std::vector<std::string> cases;  // "nav/a crowd walks …" (the suite's label, a slash, the case)
    std::vector<std::string> head;   // the first lines under the failing cases
    std::string last_passed;         // a timeout: the case that passed last before it
};

struct Member {
    std::string path;  // the workspace member, "modules/projects/xengine/game/nav/cts"
    std::vector<Failure> failures;
};

// A row of the run: a job.
struct Row {
    std::string name;                       // "test macos-15 llvm@23.1.3 ci"
    std::string conclusion;                 // "failure", "success" … (from the run's jobs; empty when unknown)
    std::vector<Member> members;            // in the order they failed
    std::vector<std::string> build_errors;  // "build failed in gpu v0.1.0 (…)" and the errors before it
    std::string summary;                    // "workspace test: 3/171 member(s) failed; …"
};

// Rows from `gh run view RUN --log-failed` (job, step, time-stamped line, tab-separated).
std::vector<Row> read_log(std::string_view log);

// A row of STATUS's CI red list.
struct Red {
    int number = 0;
    std::string failure;  // the second cell
    std::string state;    // the last cell
};

// The red list's rows (the table under "## CI red list").
std::vector<Red> read_red_list(std::string_view status);

// The names a failure goes by. Its own: the cases' words, its program, the
// suites' labels with a dot in them and the member's path. Its package's: the member's package
// and, for a `.cts` member, the package it checks.
struct Names {
    std::vector<std::string> own;
    std::vector<std::string> package;
};

Names names_of(const Member& member, const Failure& failure, std::string_view package);

// The red rows that name a failure: by its own names, or (only when none
// does) by its package's — a hint, not a match.
struct Known {
    std::vector<int> rows;          // by the failure's own names
    std::vector<int> package_rows;  // by its package's, when no row names the failure
};

Known known_in(const Names& names, std::span<const Red> reds);

// A member's package as its manifest says ("namespace.name"), or empty.
std::string package_of(std::string_view manifest);

}  // namespace omui::devtools::ci
