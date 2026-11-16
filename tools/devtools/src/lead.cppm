// omui.devtools.lead — the lead ledger's page and its check (Part 2 plan §2, D-135; moved from
// tools/eval/lead.py's `docs` and `check`). The page (docs/{en,zh}/lead.md) is the ledger's alone: the same
// ledger, the same text. The check holds the signature features to the ledger: two to four headline features
// a project, a feature with evidence names a signature-scene test that exists, a claimed one has a far_ahead
// record, the metric list well formed, and the pages current.
export module omui.devtools.lead;

import std;

export namespace omui::devtools::lead {

// The lead page in each language ("en", "zh"), from the three files' texts; or why they cannot be read.
[[nodiscard]] std::expected<std::map<std::string, std::string>, std::string> pages(std::string_view metrics, std::string_view features,
                                                                                    std::string_view ledger);

// (where, what) for each violation in the repository at `root`.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> problems(const std::filesystem::path& root);

}  // namespace omui::devtools::lead
