// omui.devtools.lanes — which of CI's lanes a run tests (D-248, D-254): the
// catalogue (.github/ci.lanes.xdef.json), a selection read from the lanes a run
// is given or from a commit's `CI:` trailer, and the plan a run follows — the
// matrix's rows and the jobs it runs, and what it requests as runs of their own.
export module omui.devtools.lanes;

import std;

export namespace omui::devtools::lanes {

// A row of the test matrix.
struct Row {
    std::string os;         // "macos-15"
    std::string toolchain;  // "llvm@23.1.3"
    std::string profile;    // "ci"
};

// A lane: rows of ci.yml's matrix, a job of ci.yml, or a workflow of its own.
struct Lane {
    std::string name;      // "macos"
    std::vector<Row> rows;
    std::string job;       // ci.yml's job; its output is named after the lane
    std::string workflow;  // "pages.yml"
    std::string what;      // what it checks
    int minutes = 0;       // runner minutes, about, all its rows together
    bool in_ci() const { return workflow.empty(); }
};

struct Group {
    std::string name;
    std::vector<std::string> lanes;
    std::string what;
};

struct Catalogue {
    std::vector<Lane> lanes;
    std::vector<Group> groups;
    std::map<std::string, std::string, std::less<>> events;  // an event's selection when it is given none
    const Lane* lane(std::string_view name) const;
};

// The catalogue from its JSON, or what is wrong with it (a lane with neither rows, a job nor a workflow;
// a group naming no lane; a name used twice).
std::expected<Catalogue, std::string> read(std::string_view text);

// The lanes a selection names, in the catalogue's order. Names are separated by spaces or commas and
// read left to right: a lane or a group adds, `-name` takes away, `none` empties. An unknown name is
// an error that lists the names there are.
std::expected<std::vector<std::string>, std::string> select(const Catalogue& catalogue, std::string_view selection);

// A commit message's `CI:` trailer — a line of its last paragraph — or nothing.
std::optional<std::string> trailer(std::string_view message);

struct Plan {
    std::string event;
    std::string selection;               // as given
    std::string source;                  // "the lanes it was given", "the commit's CI: trailer", "push's default"
    std::vector<std::string> lanes;      // selected, in the catalogue's order
    std::vector<Row> rows;               // this run's matrix
    std::vector<std::string> jobs;       // this run's jobs of ci.yml
    std::vector<std::string> requested;  // CI's lanes requested as a run of their own (a pull request's trailer)
    std::vector<std::string> workflows;  // workflows requested
    int minutes = 0;                     // runner minutes, about, of everything selected
};

// What a run of `event` tests: its lanes are `given` when given (a run by hand), else the trailer of
// the commit `message` on a pull request, else the event's default. A pull request's run tests the lint
// alone: the lanes its commit names in ci.yml are requested as a run of their own, so a later push
// does not cancel them and they follow one path with a run asked by hand. The event `request` (the
// command line's) places lanes as a pull request does.
std::expected<Plan, std::string> plan(const Catalogue& catalogue, std::string_view event, std::string_view given,
                                      std::string_view message);

// `[{"os":…,"toolchain":…,"profile":…},…]`, for the matrix's `include`.
std::string matrix_json(const Plan& plan);

// The step's outputs, one `name=value` a line: matrix, rows, and each job's lane true or false.
std::string outputs(const Catalogue& catalogue, const Plan& plan);

// What the run tests and requests, and its minutes: Markdown for the run's summary and the terminal.
std::string describe(const Catalogue& catalogue, const Plan& plan);

}  // namespace omui::devtools::lanes
