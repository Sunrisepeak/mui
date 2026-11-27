// omui-devtools rename — an internal code name renamed in one command (D-208, RC3.14): wherever it is a whole word
// in the repository's tracked files — packages (`xtools.inspector`), modules and imports, C++ namespaces
// (`xtools::`), programs (`xtools-inspector`), paths (`modules/projects/xtools/`) and the docs — and the files and
// directories it names moved. Without --write it only says what would change. What it leaves: `.agents/` (the
// decisions and the status record what was so under the names of their time). Lint verifies the result.
export module omui.devtools.rename;

import std;

export namespace omui::devtools::rename {

struct Plan {
    std::vector<std::pair<std::string, std::size_t>> edits;    // a file and how many times the name is in it
    std::vector<std::pair<std::string, std::string>> moves;    // a tracked path and where it goes
    std::vector<std::string> kept_names;  // the record's file names that hold the name: a text naming them keeps them
};

// What renaming `from` to `to` changes (both a code name: lowercase letters and digits, a letter first), or why not.
[[nodiscard]] std::expected<Plan, std::string> plan(const std::filesystem::path& root, std::string_view from, std::string_view to);
// The plan carried out: every edit written, every path moved (git mv).
[[nodiscard]] std::expected<void, std::string> apply(const std::filesystem::path& root, const Plan& plan, std::string_view from, std::string_view to);
// `text` with every whole-word `from` as `to` (a word: letters, digits and '_' on neither side).
[[nodiscard]] std::string renamed(std::string_view text, std::string_view from, std::string_view to, std::size_t* count = nullptr,
                                  std::span<const std::string> kept_names = {});

}  // namespace omui::devtools::rename
