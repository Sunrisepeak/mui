// omui.devtools.run — what the CI steps and drivers share (D-206): a program run, its output captured or passed
// through; a build through mcpp whose products are read from its own record (`--message-format json`, mcpp docs
// 50 §9), not guessed from the newest file under target/; and the machine's share a build may take (AGENTS §5:
// at most 90% of the cores, and of what 90% of the memory holds at about 2.25 GB a compile).
export module omui.devtools.run;

import std;

export namespace omui::devtools::run {

struct Ran {
    int status = 0;          // as a shell reports it; 124 where the timeout ended it (as timeout(1) says)
    std::string out;         // standard output, when captured
    bool timed_out = false;  // the timeout ended it, and everything it started
};

struct How {
    std::filesystem::path cwd;                                   // empty: the root
    bool shown = false;                                          // standard output passed through rather than captured
    bool merged = false;                                         // standard error captured with standard output
    std::vector<std::pair<std::string, std::string>> variables;  // set for the program
    std::vector<std::string> unset;                              // left out of what it inherits
    std::string input;                                           // given on its standard input (none: the null device)
    std::chrono::seconds timeout{0};                             // more than 0: ended with what it started after so long
};

// `program args…` (a bare name is looked for on PATH); or why it did not start.
[[nodiscard]] std::expected<Ran, std::string> program(const std::filesystem::path& root, std::string name, std::vector<std::string> args,
                                                      const How& how = {});

// Its output, the trailing line breaks dropped; or why it failed (did not start, or a status other than 0).
[[nodiscard]] std::expected<std::string, std::string> output(const std::filesystem::path& root, std::string name, std::vector<std::string> args,
                                                             const How& how = {});

struct Artifact {
    std::string name, kind;      // the target's name; bin, test, shared, static
    std::filesystem::path path;  // absolute
};

// `mcpp build ARGS… --message-format json` at the root, within the machine's share (MCPP_JOBS), its narration
// on standard error; the products it reports, or why it failed.
[[nodiscard]] std::expected<std::vector<Artifact>, std::string> build(const std::filesystem::path& root, std::vector<std::string> args);

// `mcpp pack ARGS…`: the file it made that ends with `suffix` (".apk", "index.html") — a record's artifact when
// `--message-format json` gives one, else its `Packed <path>` line (what it prints for a package format; `~` is
// the home directory); nothing when it made none.
[[nodiscard]] std::optional<std::filesystem::path> packed(const std::filesystem::path& root, std::vector<std::string> args, std::string_view suffix);

// `mcpp test ARGS… --no-run --message-format json`: each test built, by its name, to its program; or why not.
[[nodiscard]] std::expected<std::map<std::string, std::filesystem::path>, std::string> tests(const std::filesystem::path& root, std::vector<std::string> args);

// The repository's commit, short (8), or "unknown".
[[nodiscard]] std::string commit(const std::filesystem::path& root);

// Today, YYYY-MM-DD (UTC): the date a record carries.
[[nodiscard]] std::string today();

// How many compiles at once keep a build within 90% of this machine; a lower MCPP_JOBS already set is kept.
[[nodiscard]] int share_of_machine();

}  // namespace omui::devtools::run
