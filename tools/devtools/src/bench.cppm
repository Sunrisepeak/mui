// omui.devtools.bench — the comparisons with the best of each field, driven (tests/bench/competitors; moved
// from its run.sh, D-206). Our side is built natively and for wasm32-emscripten (omui.bench.competitors); the
// competitors' own runtimes — GSAP, Skia and resvg in node, ThorVG and rlottie through their Python
// bindings — are installed at their pinned versions in target/bench-competitors and run as programs. The Web's
// sides run in the same node, so the same V8.
//
// Without rounds: every comparison three times, printed (the README's tables). With rounds: the lead ledger
// (Part 2 plan §2.2, D-135) — our side and theirs alternating round after round in this one run, each line a
// sample (omui.devtools.lead), each metric judged by the ratio a round, the ledger and its pages written.
export module omui.devtools.bench;

import std;

export namespace omui::devtools::bench {

struct Options {
    int rounds = 0;        // 0: the tables
    bool dry_run = false;  // with rounds: the verdicts printed, nothing written
};

// The run's exit status, or why it could not run.
[[nodiscard]] std::expected<int, std::string> competitors(const std::filesystem::path& root, const Options& options);

// The markdown corpus: what people wrote in the repository (docs/, site/content/, each file in path order,
// joined by a blank line), repeated until it is at least `size` bytes.
[[nodiscard]] std::string corpus(const std::filesystem::path& root, std::size_t size = std::size_t{1} << 20);

}  // namespace omui::devtools::bench
