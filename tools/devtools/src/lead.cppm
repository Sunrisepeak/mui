// omui.devtools.lead — the lead ledger (Part 2 plan §2, D-135; moved from tools/eval/lead.py, D-206): our
// side against the strongest competitor, measured on one machine in the same job, judged by ratio.
//
// sample      a benchmark program's JSON lines, tagged with the series, the round and the key
// verdict     every measured metric: the ratio a round (theirs ÷ ours when lower is better), its median and a
//             95 % bootstrap interval, the strongest competitor (the lowest median ratio), the verdict from the
//             interval's lower end — an interval across a threshold takes the worse tier
// capability  a capability against competitors (none | partial | app-defined | full)
// pages       the site's lead page (docs/{en,zh}/lead.md) and the evaluation summary, the ledger's alone: each
//             dated by the ledger's newest record, so the same ledger gives the same text
// problems    the signature features held to the ledger (lint `lead`): two to four headline features a project,
//             a feature with evidence names a signature-scene test that exists, a claimed one has a far_ahead
//             record, the metric list well formed, and the pages current
export module omui.devtools.lead;

import std;

export namespace omui::devtools::lead {

inline constexpr std::string_view features_path = ".agents/docs/evaluation/lead.features.omui.json";
inline constexpr std::string_view ledger_path = ".agents/docs/evaluation/lead.ledger.omui.jsonl";
inline constexpr std::string_view metrics_path = "tests/bench/competitors/lead.metrics.omui.json";

// Ratio thresholds (§2.2): r = ours ÷ the strongest competitor, larger better.
inline constexpr double behind = 0.95, ahead = 1.2, far = 10.0;

// "behind", "par", "ahead" or "far_ahead".
[[nodiscard]] std::string_view tier(double ratio);

// The median of `values` (the mean of the middle two when they are even); 0 for none.
[[nodiscard]] double median(std::vector<double> values);

// The median's 95 % bootstrap interval: seeded, its own generator, so a rerun anywhere says the same.
[[nodiscard]] std::pair<double, double> interval(std::span<const double> values, std::uint64_t seed = 20261108, int resamples = 4000);

// One sample a JSON line `output` printed: {"series", "round", "key", and its numeric fields} as JSON text.
// The series is the line's `series_field` where it has one; a vector comparison's line (a renderer and
// "X against Y" pictures) gives one sample a renderer and an error sample each; otherwise `series`.
[[nodiscard]] std::vector<std::string> samples(std::string_view output, std::string_view series, std::string_view series_field,
                                               std::string_view key, int round);

// Where and by what a verdict was made.
struct Context {
    std::string date;     // YYYY-MM-DD
    std::string machine;  // host-threads
    std::string commit;   // the short hash
};

struct Verdict {
    std::string record;  // the ledger line (JSON)
    std::string said;    // one line for a person
};

// A record a metric of `metrics` the samples measure, and one for each newly unverified metric (one the ledger
// does not already hold as unverified); `skipped` gets each measured metric no sample pairs.
[[nodiscard]] std::expected<std::vector<Verdict>, std::string> verdicts(std::string_view metrics, std::string_view samples_text,
                                                                         std::string_view ledger, const Context& context,
                                                                         std::vector<std::string>& skipped);

inline constexpr std::array supports{"none", "partial", "app-defined", "full"};

// far_ahead when none has it, ahead when some have it only in part or by the application's own code, par when
// one has it whole.
[[nodiscard]] std::string_view capability_verdict(std::span<const std::string> support);

// The site's lead page in each language ("en", "zh") and the evaluation summary ("summary"), keyed by the
// path they are written to; or why the three files cannot be read.
[[nodiscard]] std::expected<std::map<std::string, std::string>, std::string> pages(std::string_view metrics, std::string_view features,
                                                                                    std::string_view ledger);

// Today (UTC), this machine (host-threads) and the commit at `root`.
[[nodiscard]] Context context_here(const std::filesystem::path& root);

// The lines appended to `path` (its directory made); false when it cannot be written.
[[nodiscard]] bool append(const std::filesystem::path& path, std::span<const std::string> lines);

// pages() written under `root`: the paths written, or why not.
[[nodiscard]] std::expected<std::vector<std::string>, std::string> write_pages(const std::filesystem::path& root);

// (where, what) for each violation in the repository at `root`.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> problems(const std::filesystem::path& root);

}  // namespace omui::devtools::lead
