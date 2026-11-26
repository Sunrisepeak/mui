// omui.devtools.closure — the closure checklist of each project (release plan §5.1, gate 3), checked from the tree:
// the ten items each stable or preview project passes (a preview one within its stated scope) and the four starred
// items an experimental one passes. Every item is read from what the repository holds — a README's definition line,
// its feature table, its Known limitations, its exceptions; the packages' API surfaces and tests; catalogs and skills;
// the bench budgets; the claims ledger; the consumer probes; lint's agent-surface rule — never typed into a table.
//
// What a README says, in the forms read here:
//   - its first line under the title: one sentence, the project's definition;
//   - a feature table whose header has a `Label` column: each row a label (stable, preview, experimental) and a link;
//   - `## Known limitations`: each item `- **<what>** (<level>): …`, the level the one it is solved at (D-179:
//     foundation, part, specialised, application);
//   - `## Closure exceptions`: each item `- **<number>**: <why>` — an item that does not apply, said so (plan §8.3).
export module omui.devtools.closure;

import std;

export namespace omui::devtools::closure {

enum class Mark : std::uint8_t { pass, owed, exempt, not_required };

struct Cell {
    Mark        mark = Mark::owed;
    std::string note;  // what passed it, what is owed, or why it is exempt
};

inline constexpr std::size_t item_count = 10;
inline constexpr std::array<std::string_view, item_count> item_names{
    "definition and features", "API review", "errors", "agent surface", "ops and skill",
    "tests", "benchmarks", "side by side", "independent use", "known limitations"};
inline constexpr std::array<bool, item_count> starred{true, false, false, true, false, true, false, false, false, true};

struct Project {
    std::string                    name;    // omui, hanim, mrender, meng, gongfu, xtools, mlibs
    std::string                    readme;  // from the root
    std::string                    label;   // its most mature package's
    std::array<Cell, item_count>   items;
    [[nodiscard]] std::size_t passed() const;
    [[nodiscard]] std::size_t owed() const;
};

struct Report {
    std::vector<Project> projects;
    [[nodiscard]] std::size_t passed() const;
    [[nodiscard]] std::size_t owed() const;
};

// Every project's checklist, read from the tree under `root`.
std::expected<Report, std::string> check(const std::filesystem::path& root);

// The report as a page (docs/{en,zh}/closure.md): a row a project, a column an item, then what each owes.
std::string page(const Report& report, std::string_view language);

// The pieces, each read alone (and tested so).
// The first line of prose under a README's title, if it is one sentence.
std::optional<std::string> definition(std::string_view readme);
// A feature table's rows lacking a label or a link (empty: every row has both; nullopt: no such table).
std::optional<std::vector<std::string>> features_owed(std::string_view readme);
// Known limitations' items lacking a level (nullopt: no such section; empty: every item has one).
std::optional<std::vector<std::string>> limitations_owed(std::string_view readme);
// The items a README exempts: number → why.
std::map<std::size_t, std::string> exceptions(std::string_view readme);

}  // namespace omui::devtools::closure
