// omui.devtools.bench — the comparisons with the best of each field, driven (tests/bench/competitors; moved
// from its run.sh, D-206). Our side is built natively and for wasm32-emscripten (omui.bench.competitors); the
// competitors' own runtimes — GSAP, Skia and resvg in node, ThorVG and rlottie through their Python
// bindings — are installed at their pinned versions in target/bench-competitors and run as programs. The Web's
// sides run in the same node, so the same V8.
//
// Without rounds: every comparison three times, printed (the README's tables). With rounds: the lead ledger
// (Part 2 plan §2.2, D-135) — our side and theirs alternating round after round in this one run, each line a
// sample (omui.devtools.lead), each metric judged by the ratio a round, the ledger and its pages written.
//
// `apps` (C1, N2; lead design §2a.1): whole applications side by side — omui's in each world, Slint's, Flutter's
// and NSH's — built at pinned versions (tests/bench/competitors/apps), each launched in a window of an X server of
// the harness's own and measured from outside its process: start to the first presented frame (cold and warm),
// memory at rest (private dirty, USS, PSS, RSS), the processor and the frames at rest, a click to the next
// presented frame; the program's and the package's bytes. Rounds alternate the sides; the ledger judges them.
export module omui.devtools.bench;

import std;
import mcpplibs.cmdline;

export namespace omui::devtools::bench {

struct Options {
    int rounds = 0;        // 0: the tables
    bool dry_run = false;  // with rounds: the verdicts printed, nothing written
};

// The run's exit status, or why it could not run.
[[nodiscard]] std::expected<int, std::string> competitors(const std::filesystem::path& root, const Options& options);

// Material 3's controls in four states captured from their page
// (tests/bench/competitors/compare/material3, once its bundle is built) into
// OUT/<control>-<state>.png: the exit status, or why it could not run.
[[nodiscard]] std::expected<int, std::string> material3(const std::filesystem::path& root,
                                                        const std::filesystem::path& out);

// The markdown corpus: what people wrote in the repository (docs/, site/content/, each file in path order,
// joined by a blank line), repeated until it is at least `size` bytes.
[[nodiscard]] std::string corpus(const std::filesystem::path& root, std::size_t size = std::size_t{1} << 20);

// `bench apps`: its command line, and the run (its exit status).
[[nodiscard]] mcpplibs::cmdline::App apps_command_line();
[[nodiscard]] int apps(const std::filesystem::path& root, const mcpplibs::cmdline::ParsedArgs& given);

// What `pmap -XX` says a process holds, in bytes: its totals line read by the header's names.
struct Memory {
    std::uint64_t rss = 0, pss = 0, uss = 0, private_dirty = 0, swap = 0;
};
[[nodiscard]] std::optional<Memory> memory_of(std::string_view pmap_output);

// The context switches a second of a process's threads (`pidstat -w -t`'s averages, voluntary and not).
[[nodiscard]] std::optional<double> switches_of(std::string_view pidstat_output);
// The threads that woke in it, by name, each its switches a second.
[[nodiscard]] std::map<std::string, double> waking_of(std::string_view pidstat_output);

}  // namespace omui::devtools::bench
