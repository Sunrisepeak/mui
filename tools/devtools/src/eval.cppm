// omui.devtools.eval — the evaluations that use what they judge (moved from tools/eval, D-206). Each asks a real
// agent (a fresh `claude -p` session, or codex) in an empty directory of its own, and checks the outcome on the
// subject's own state or numbers — never on the agent's word; an agent that cannot run (a quota, a login) is
// recorded as unavailable, neither a failure nor a pass.
//
// compare    the side-by-side reviews (D-135, rubric compare-v1): blinded sheets of our picture beside a similar
//            project's, judged by fresh sessions that have nothing of the project, unblinded into
//            ahead / par / behind; the maintainer's verdict recorded, which decides.
export module omui.devtools.eval;

import std;
import mcpplibs.cmdline;
import nlohmann.json;
import mlibs.image;

export namespace omui::devtools::eval {

// The `eval` command: its declaration, and a parsed line run (the exit status).
[[nodiscard]] mcpplibs::cmdline::App command();
[[nodiscard]] int run(const mcpplibs::cmdline::ParsedArgs& options, const std::filesystem::path& root);

// ---- what every evaluation shares

// A line appended to a ledger (one JSON object a line, `", "` and `": "` between, as the ledgers are written).
[[nodiscard]] std::expected<void, std::string> append(const std::filesystem::path& ledger, const nlohmann::ordered_json& record);

// The record's JSON as a ledger line holds it.
[[nodiscard]] std::string ledger_line(const nlohmann::ordered_json& record);

// One fresh agent session: `claude -p PROMPT --output-format json` (or `codex exec`) in `directory`, with only the
// tools named; its final text and its usage, or why it was unavailable.
struct Session {
    std::string agent = "claude";             // claude or codex
    std::filesystem::path directory;          // where it runs (an empty one of its own)
    std::vector<std::string> tools;           // claude's --allowedTools
    std::vector<std::string> extra;           // more of the agent's arguments (an MCP configuration)
    std::chrono::seconds timeout{600};
};
struct Reply {
    std::string text;                // the final message
    std::vector<std::string> tools;  // the tools it called, in order
    std::vector<std::string> commands;  // what its shell tool ran, in order (a Bash call's command)
    int turns = 0;
    nlohmann::json usage;            // as the agent reports it
    nlohmann::json raw;              // claude's result event
};
// Its reply, or why the agent was unavailable (did not start, a quota or login, no result, the timeout).
[[nodiscard]] std::expected<Reply, std::string> ask(const Session& session, std::string_view prompt);

// A session's tokens: cache reads (the history read again each turn, at about a tenth of the price) kept apart from
// the fresh ones (input, cache writes, output), which are what it costs.
struct Tokens {
    std::int64_t input = 0, cache_write = 0, cache_read = 0, output = 0;
    [[nodiscard]] std::int64_t fresh() const noexcept { return input + cache_write + output; }
};
[[nodiscard]] Tokens tokens_of(const nlohmann::json& usage);

// The first JSON object in a text (an agent's answer often has words around it).
[[nodiscard]] std::optional<nlohmann::json> first_object(std::string_view text);

// ---- compare

inline constexpr std::array<std::string_view, 3> verdicts{"behind", "par", "ahead"};

// A judge's winner (A, B or even) read against which side was ours.
[[nodiscard]] std::string unblind(std::string_view winner, std::string_view ours);

// The majority of a sheet's votes; a tie is par.
[[nodiscard]] std::string majority(std::span<const std::string> votes);

// A dimension is as good as its weakest pair.
[[nodiscard]] std::string weakest(std::span<const std::string> pair_verdicts);

// Two pictures side by side on white under a band, labelled A and B (5 × 7 cells, the only words a sheet carries);
// either may have alpha, laid over white.
[[nodiscard]] mlibs::image::Image sheet(const mlibs::image::Image& left, const mlibs::image::Image& right);

// Pictures in a row on white, `gap` apart.
[[nodiscard]] mlibs::image::Image row(std::span<const mlibs::image::Image> pictures, int gap = 0);

struct Judging {
    std::string agent = "claude";
    int samples = 3;
    bool dry_run = false;
};
[[nodiscard]] int compare_sheets(const std::filesystem::path& root, std::string_view subject);
[[nodiscard]] int compare_judge(const std::filesystem::path& root, std::string_view subject, const Judging& judging);
[[nodiscard]] int compare_record(const std::filesystem::path& root, std::string_view subject, std::string_view dimension, std::string_view verdict,
                                 std::string_view by, std::string_view note);

// ---- tasks (Part 2 plan §6.3): a goal in words, the outcome checked on the application's own state

struct Tasks {
    std::vector<std::string> only;  // these tasks' ids (none: every one)
    std::string via = "semantic";   // semantic, a11y-tree or screenshot (omui mcp --way)
    std::string agent = "claude";
    int trials = 1;
    bool dry_run = false;           // the records printed, not appended to tasks.eval.xdef.jsonl
};
// The set tools/eval/tasks/SET.tasks.xdef.json, each task on a fresh instance of its program.
[[nodiscard]] int tasks_run(const std::filesystem::path& root, std::string_view set, const Tasks& options);
// The harness alone, once (C7): the same agent with no tool server and a goal that needs nothing; a line of the
// ledger (via "none") that the summary takes from each way's tokens, to say what a task cost that way.
[[nodiscard]] int tasks_baseline(const std::filesystem::path& root, std::string_view set, const Tasks& options);
// Each way's latest trial of every task of the set (from the ledger), side by side.
[[nodiscard]] int tasks_summary(const std::filesystem::path& root, std::string_view set, bool split_only);

// ---- animate (§10.3): a real agent animates each brief without a GUI; the document graded by its samples

struct Animate {
    std::vector<std::string> only;  // these briefs (none: every one)
    int trials = 1;
    int parallel = 3;
    std::string agent = "claude";   // claude or codex
    std::string model;              // claude: sonnet by default
    std::string effort = "medium";  // codex's reasoning effort
    std::chrono::seconds timeout{1500};
    std::filesystem::path keep;     // where the trials' directories stay (target/eval/animate)
    std::filesystem::path grade;    // grade this one kept trial instead (its brief from its name)
    bool no_ledger = false;
};
[[nodiscard]] int animate(const std::filesystem::path& root, const Animate& options);

// ---- duel: two codex agents play the fighting game by turns; a third analyses the recording

struct Duel {
    std::filesystem::path root;
    std::string p1 = "kata", p2 = "mo";  // fighters by their files' names
    int frames = 10;                     // a turn's frames
    int turns = 150;                     // at most this many
    std::chrono::seconds timeout{3600};  // the players' time
    std::filesystem::path analyse;       // a recording's directory to analyse again (no play)
    std::string date;                    // the report's (default: today)
};
[[nodiscard]] int duel(const Duel& duel);

// ---- materials: what a comparison set compares

// The controls (compare set `components`): ours from the site's quality lab, Material 3's from @material/web in
// the same four states, cells and background; both under target/compare-material/{omui,material3}/<control>.png.
[[nodiscard]] int materials_controls(const std::filesystem::path& root);

// ---- cold-start (D-186, D-192): a fresh agent, one program it may run, one task in words; checked on the state

struct ColdStart {
    std::vector<std::string> only;  // these tasks (none: every one)
    std::string agent = "claude";
    int trials = 1;
    bool dry_run = false;           // the records printed, not appended to coldstart.eval.xdef.jsonl
    bool keep = false;              // leave each trial's directory (the state and the agent's)
};
// The set tools/eval/coldstart/SET.coldstart.xdef.json.
[[nodiscard]] int cold_start(const std::filesystem::path& root, std::string_view set, const ColdStart& options);

// ---- hot-reload (W2: save to screen ≤ 100 ms at p95)

struct HotReload {
    std::filesystem::path omui;     // the omui command
    std::filesystem::path program;  // the application's program (run with --dev --serve)
    std::filesystem::path app_dir;  // its directory: mcpp.toml and logic/ are copied, the copy's logic/main.ts rewritten
    int rounds = 20;
};
// Prints {"rounds", "median_ms", "p95_ms", "max_ms"}; 1 when a round does not show within 5 s.
[[nodiscard]] int hot_reload(const HotReload& options);

}  // namespace omui::devtools::eval
